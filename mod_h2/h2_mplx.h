/* Copyright 2015 greenbytes GmbH (https://www.greenbytes.de)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __mod_h2__h2_mplx__
#define __mod_h2__h2_mplx__

/**
 * The stream multiplexer. It pushes h2_buckets from the connection
 * thread (httpd worker) to the stream task threads and vice versa.
 *
 * Currently there is no forced join between ongoing task threads and
 * the main connection thread. So h2_mplx implements reference counting
 * and auto-destructs when the last reference goes away.
 *
 * There is one h2_mplx instance for each h2_session, which sits on top
 * of a particular httpd conn_rec. Input goes from the connection to
 * the stream tasks. Output goes from the stream tasks to the connection,
 * e.g. the client.
 *
 * Each h2_bucket is associated with a particular stream identifier (the
 * id from the HTTP2 protocol). It is possible to read for a particular
 * stream id or do other operations connected to it.
 *
 * For each stream, there can be at most "H2StreamMaxMemSize" bytes
 * queued in the multiplexer. If a task thread tries to write more
 * data, it is blocked until space becomes available.
 *
 * Writing input is never blocked. The HTTP2 flow control will prevent
 * too much data becoming available.
 */

struct apr_pool_t;
struct apr_thread_mutex_t;
struct apr_thread_cond_t;
struct h2_bucket;
struct h2_config;
struct h2_response;

typedef struct h2_mplx h2_mplx;

/* Create the multiplexer for the given HTTP2 session.
 * The created multiplexer already has a reference count of 1.
 */
h2_mplx *h2_mplx_create(conn_rec *c, apr_pool_t *master);

/* Destroy and cleanup the multiplexer. Automatically called when
 * the reference count to this multiplexer goes to 0.
 */
void h2_mplx_destroy(h2_mplx *mplx);

/* Get the memory pool used by the multiplexer.
 */
apr_pool_t *h2_mplx_get_pool(h2_mplx *mplx);

/* Get the main connection this multiplexer works for */
conn_rec *h2_mplx_get_conn(h2_mplx *mplx);

/* Abort the multiplexer. It will answer all invocation with
 * APR_ECONNABORTED afterwards.
 */
void h2_mplx_abort(h2_mplx *mplx);

/* Get the id of the multiplexer */
long h2_mplx_get_id(h2_mplx *mplx);

apr_status_t h2_mplx_start_io(h2_mplx *mplx, int stream_id);
void h2_mplx_end_io(h2_mplx *mplx, int stream_id);

/* Read a h2_bucket for the given stream_id. Will return ARP_EAGAIN when
 * called with APR_NONBLOCK_READ and no data present. Will return APR_EOF
 * when the input of the stream has been closed.
 */
apr_status_t h2_mplx_in_read(h2_mplx *mplx, apr_read_type_e block,
                             int stream_id, struct h2_bucket **pbucket);

/* Add data to the input of the given stream. Storage of input data is
 * not subject to flow control.
 */
apr_status_t h2_mplx_in_write(h2_mplx *mplx,
                              int stream_id, struct h2_bucket *bucket);

/* Closes the input for the given stream_id.
 */
apr_status_t h2_mplx_in_close(h2_mplx *m, int stream_id);

/* Indicates that the input for the given stream has been closed. There
 * might still be data to be read, but it can be read without blocking.
 */
int h2_mplx_in_has_eos_for(h2_mplx *m, int stream_id);

/* Read output data from the given stream. Will never block, but
 * return APR_EAGAIN until data arrives or the stream is closed.
 */
apr_status_t h2_mplx_out_read(h2_mplx *mplx, int stream_id, 
                              struct h2_bucket **pbucket, int *peos);

/* Opens the output for the given stream with the specified response.
 */
apr_status_t h2_mplx_out_open(h2_mplx *mplx, int stream_id,
                              struct h2_response *response);

/* Writes data to the output of the given stream. With APR_BLOCK_READ, it
 * is subject to flow control.
 */
apr_status_t h2_mplx_out_write(h2_mplx *mplx, apr_read_type_e block,
                               int stream_id, struct h2_bucket *bucket);

/* Closes the output stream. Readers of this stream will get all pending 
 * data and then only APR_EOF as result. 
 */
apr_status_t h2_mplx_out_close(h2_mplx *m, int stream_id);

/* Reset the given stream. Indicate, which error occured, if any.
 */
apr_status_t h2_mplx_out_reset(h2_mplx *m, int stream_id, apr_status_t status);

/* Wait on output data from any stream to become available. Returns
 * APR_TIMEUP if no data arrived in the given time.
 */
apr_status_t h2_mplx_out_trywait(h2_mplx *m, apr_interval_time_t timeout);

/* Return != 0 iff the multiplexer has data for the given stream. 
 */
int h2_mplx_out_has_data_for(h2_mplx *m, int stream_id);

/* Get the response for an opened stream. Will return a response
 * only once for a particular stream. The stream this response
 * belongs to will be open for reading.
 */
struct h2_response *h2_mplx_pop_response(h2_mplx *m);

#endif /* defined(__mod_h2__h2_mplx__) */
