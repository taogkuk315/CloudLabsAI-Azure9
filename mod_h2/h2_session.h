/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef __mod_h2__h2_session__
#define __mod_h2__h2_session__

#include "h2_io.h"

/**
 * A HTTP/2 connection, a session with a specific client.
 * 
 * h2_session sits on top of a httpd conn_rec* instance and takes complete
 * control of the connection data. It receives protocol frames from the
 * client. For new HTTP/2 streams it creates h2_task(s) that are sent
 * via callback to a dispatcher (see h2_conn.c).
 * h2_session keeps two h2_bucket_queue instances, one for the incoming
 * HEADER and DATA payload and one for the outgoing DATA payload.
 *
 * New incoming HEADER frames are converted into a h2_stream+h2_task instance
 * that both represent a HTTP/2 stream, but may have separate lifetimes. This
 * allows h2_task to be scheduled in other threads without semaphores
 * all over the place. It allows task memory to be freed independant of
 * session lifetime and sessions may close down while tasks are still running.
 *
 *
 */

struct h2_config;
struct h2_mplx;
struct h2_resp_head;
struct h2_session;
struct h2_stream;
struct h2_task;

struct nghttp2_session;

typedef struct h2_session h2_session;

/* Callback when a new task for a stream has been created. The callback
   should schedule the task for execution and return immediately. */
typedef void on_new_task(h2_session *session,
                         struct h2_stream *stream,
                         struct h2_task *task);

/* Callback when a stream is closed and will be destroyed. */
typedef void on_stream_close(h2_session *session, struct h2_stream *stream);

struct h2_session {
    long id;                        /* identifier of this session, unique
                                     * inside a httpd process */
    conn_rec *c;                    /* the connection this session serves */
    request_rec *r;                 /* the request that started this in case
                                     * of 'h2c', NULL otherwise */
    int aborted;                    /* this session is being aborted */
    
    h2_io_ctx io;                   /* io on httpd conn filters */
    struct h2_mplx *mplx;           /* multiplexer for stream data */
    struct h2_stream_set *streams;  /* streams handled by this session */
    
    on_new_task *on_new_task_cb;    /* notify of new h2_task creations */
    on_stream_close *on_stream_close_cb; /* notify that stream was closed */

    int loglvl;
    
    struct nghttp2_session *ngh2;   /* the nghttp2 session (internal use) */

};


/* Create a new h2_session for the given connection (mode 'h2').
 * The session will apply the configured parameter.
 */
h2_session *h2_session_create(conn_rec *c, struct h2_config *cfg);

/* Create a new h2_session for the given request (mode 'h2c').
 * The session will apply the configured parameter.
 */
h2_session *h2_session_rcreate(request_rec *r, struct h2_config *cfg);

/* Destroy the session and all object it still contains. This will not
 * destroy h2_task instances that not finished yet. */
void h2_session_destroy(h2_session *session);

/* Called once at start of session. Performs initial client thingies. */
apr_status_t h2_session_start(h2_session *session);

/* Return != 0 iff session is finished and connection can be closed.
 */
int h2_session_is_done(h2_session *session);

/* Called when the session will shutdown after all open streams
 * are handled. New streams will no longer be accepted. 
 * Call with reason APR_SUCCESS to initiate a graceful shutdown. */
apr_status_t h2_session_goaway(h2_session *session, apr_status_t reason);

/* Called when an error occured and the session needs to shut down.
 * Status indicates the reason of the error. */
apr_status_t h2_session_abort(h2_session *session, apr_status_t reason);

/* Read more data from the client connection. Used normally with blocking
 * APR_NONBLOCK_READ, which will return APR_EAGAIN when no data is available.
 * Use with APR_BLOCK_READ only when certain that no data needs to be written
 * while waiting. */
apr_status_t h2_session_read(h2_session *session, apr_read_type_e block);

/* Write data out to the client, if there is any. Otherwise, wait for
 * a maximum of timeout micro-seconds and return to the caller. If timeout
 * occurred, APR_TIMEUP will be returned.
 */
apr_status_t h2_session_write(h2_session *session,
                              apr_interval_time_t timeout);

/* Start submitting the response to a stream request. This is possible
 * once we have all the response headers. */
apr_status_t h2_session_submit_response(h2_session *session,
                                        struct h2_stream *stream,
                                        struct h2_resp_head *head);

/* Set the callback to be invoked when new h2_task instances are created.  */
void h2_session_set_new_task_cb(h2_session *session, on_new_task *cb);

/* Set the callback to be invoked when a stream has been closed and
 * will be destroyed.  */
void h2_session_set_stream_close_cb(h2_session *session, on_stream_close *cb);

/* Get the h2_stream for the given stream idenrtifier. */
struct h2_stream *h2_session_get_stream(h2_session *session, int stream_id);

/* Get the first h2_stream that has a response ready and is submitted
 * yet. */
struct h2_resp_head *h2_session_pop_response(h2_session *session);

void h2_session_log_stats(h2_session *session);

#endif /* defined(__mod_h2__h2_session__) */
