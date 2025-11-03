# mod_h2 internals

A description of how the module's structure, the main terms used and how it overtall works.

## terms

 * `c1`: a *primary* connection. This is a connection to a HTTP/2 client.
 * `c2`: a *secondary* connection. This is an internal, virtual one used to process a request.
 * `session`: the HTTP/2 state for a particular `c1` connection.
 * `stream`: a HTTP/2 stream that (commonly) carries a request+body and delivers a response+body. Each stream has a unique 32 bit identifier, as defined in the HTTP/2 protocol. Stream 0 is the c1 connection itself.
 * `mplx`: the multiplexer, one per `session`. It processes streams, forwards input (request bodies) and collects output from processing. To process a stream, it creates a `c2` connection.
 * `worker`: polling all registered `mplx`s for `c2` connections to process. `mplx`s registers themselves at the workers when they have something to process.
 * `slot`: a particular worker thread. The number of workers may vary based on server load and configuration.
 * `beam`: a mechanism for transferring APR's *buckets* between `c1`and `c2`. More under memory.

## file structure

Source files are all prefixed with `h2_` followed by the main topic they are about. `h2_c2_filter` for example contains all input/output filter code for `c2` connections. `h2_session` is all about the `session` instances. etc.

## session states

HTTP/2 sessions can be in one of the following states:

 * `INIT`: state during initialization of the session. Sends inital SETTINGS to client and transits to `BUSY` on success.
 * `BUSY`: reading `c1` input, writing frames on `c1` output and checking any `c2` for I/O events. Switches to `WAIT` when `c1` input is exhausted.
 * `WAIT`: collects `c1` socket and all `c2` pipes into a pollset and does a wait with connection timeout on events. Transits on `c1` input to `BUSY` again.
 * `IDLE`: there are no streams to process. The session waits on new `c1` input to arrive. If a stream has been processed already, this is like a HTTP/1 *keepalive*.
 * `DONE`: session is done processing streams and shuts down. Possibly a last GOAWAY frame is being sent. Transits to `CLEANUP` when the protocol needs have been taken care of.
 * `CLEANUP`: release all internal resources. Make sure that any ongoing `c2` processing terminates.

There a sub-states in the `BUSY` and `WAIT` handling. A session may chose to no longer accept new streams from the clients while finishing processing all ongoing streams. This is, for example, triggered by a graceful shutdown of the child process.
 
Errors and timeouts on the `c1` connection trigger a transition to `DONE`.
 
 
## stream states

These mostly correspond to the states described in the HTTP/2 standard, with some additions for internal handling:

 * `IDLE`: a stream has been created by a request from the client.
 * `OPEN`: all request headers have arrived. The stream can start processing.
 * `RSVD_R`: the stream (identifier) has been reserved by the client (remote).
 * `RSVD_L`: the stream (identifier) has been reserved by the session (locally).
 * `CLOSED_R`: stream was closed by the client (remote). A (possibly empty) request body is complete.
 * `CLOSED_L`: stream was closed by the session (locally) and the output is complete.
 * `CLOSED`: both stream *ends* have been closed.
 * `CLEANUP`: the session is done with the stream, its resources may be reclaimed. Such a stream is handed over to the `mplx` which performs the reclamation. This needs to take care of a potentially still running `c2` connection.

A `mplx` maintains three `stream` lists:

 * `streams`: the active streams which are being processed (or scheduled to be).
 * `shold`: the streams in `CLEANUP` which have an ongoing `c2` connection that needs to terminate first.
 * `spurge`: the streams without or with a finished `c2` that can be reclaimed.
 
## memory

### setup

The APR memory model with its `pools` determines much of `mod_h2`'s structure, initialization and resource reclamation strategies. `pools` are the foundation of everything in Apache `httpd`: lists, tables, files, pipes, sockets, data and meta data transfers (`bucket`s) are tied to them.

The fundamental restriction of `pools` is that they are not thread safe. Using the same pool from 2 threads will mess up its internal lists. The more busy the server is, the more likely this will then happen. Since everything one does with APR's features has the potential to modify its underlying pool, all the things listed above are not thread safe.

Closing a file will modify the pool it was opened with, for example. If 10 files are opened with the same pool, one is unable to use 5 of them in one thread and the rest in another. Everything that is based on the same pool needs to stay on the same thread.

A `session` handling `c1` needs to run in parallel to request processing on `c2` connections. That means `session` and `c2`s have completely seprate pools. 

When a session creates a stream, it creates a new *child* pool for it. Pool memory can only be freed by destroying the whole pool. To handle thousands of streams without leaking memory, they have to be placed in child pools that can be reclaimed when a stream is done.

A `mplx` is used by the `session` *and* by `c2` connections. To manage its internal structures, it also needs its own, separate pool. This one is protects via its `mutex`. It creates new `c2` connections when needed also with separate pools as processing happens in separate `worker` threads.

All these *separate* pools have their own APR `allocator` (the one that manages system memory) to be independant. However, there are still tied with a *parent/child* relationship to not lose track of them (leaking). So `c2` pools are children of `mplx` pool which is a child of the `session` pool.

### teardown

When destroying a pool, it modifies its parent pool. When reclaiming a `c2` pool, the `mplx` pool will be changed. So, this can only be allowed to happen inside the `mplx` mutex protection. When reclaiming the `mplx`, it modifies the `session` pool. So this may only happen on the thread that works on `session`.

This means that tearing down a `session` needs to tear down the `mplx` which needs to tear down all its `c2` connections first. Or else.

### stream memory

Streams have their own input/output buffers, allocated from their own pool. Similar to "normal" HTTP/1 requests, they need to take care that all their outgoing data on `c1` has actually been sent, before they can be destroyed. HTTP/1 uses the `EOR` meta bucket for that. HTTP/2 has a `H2_EOS` bucket that is similar.

On closing a stream, a `H2_EOS` is created and send on `c1`. When this bucket is destroyed, the stream is handed to `mplx` for safe destruction. `mplx` then removes the stream from its list of active ones. It places it on its `spurge` list when the stream has no `c2`or it has already returned from the `worker`. For an active `c2`, the stream is placed into `shold`.

When a `worker` tells an `mplx` that it has finished a `c2`, the `mplx` checks if the stream is still active or if it is found in `shold`. If the stream is in `shold`, it is moved to `spurge`. It cannot be destroyed right away, since the stream's pool is a child of `session`. That would manipulate the session pool from inside a worker thread.

The purge list is instead only processed, when `session` calls the `mplx`. 

### data transfer

With all this pool touchiness, how does request/response/bodies ever get transferred between a `stream` and its `c2` connection that does the actual work? That merits its own chapter about `bucket beams`.


## bucket beams

Apache httpd uses APR's `bucket brigade`s to transfer data and meta information through its connection filters. So, whatever also one does, ultimately `streams` and `c2` connections will use brigades.

The difficulty is: **it is impossible to transfer a bucket from one brigade to another between threads**.

A bucket belongs to a `bucket_alloc` which belongs to a memory pool. All three are not thread safe and tied. Imagine transferring from brigade `b1` on thread `t1` into brigade `b2` on thread `t2`:

 * `t1` can take data out of `b1`, but cannot put it into `b2`.
 * `t2` can put data into `b2`, but cannot take it out of `b1`.

So, mod_h2 needs something to juggle the data in between `t1` and `t2` doing their thing. That is the job of a bucket beam.

### rings

A bucket beam has three APR `ring`s. Rings are a doubly linked list that works **independant** from pools. Yay!

 * `buckets_to_send`: when `t1` calls `h2_beam_send(beam, b1)`, the beam takes buckets out of `b1` and appends them to this ring. 
 * `buckets_in_flight`: when `t2` calls `h2_beam_receive(beam, b2)`, it takes buckets from `buckets_to_send`, magically creates stand-in buckets for them, puts the stand-ins into `b2` and the original ones into this ring.
 * `buckets_consumed`: when the stand-in buckets get destroyed (by `t2`) they call home and the original ones get transferred from `buckets_in_flight` to here.

The buckets in `buckets_consumed` can then be destroyed the next time that thread `t1` calls.