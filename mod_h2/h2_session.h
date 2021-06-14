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

struct apr_thread_mutex_t;
struct apr_thread_cond_t;

struct h2_config;
struct h2_session;
struct h2_stream;
struct h2_task;

struct nghttp2_session;

typedef void on_new_task(struct h2_session *session,
                         int stream_id, struct h2_task *task);

typedef struct h2_session {
    int id;                         /* identifier of this session, unique
                                     * inside a httpd process */
    conn_rec *c;                    /* the connection this session serves */

    int aborted;                    /* this session is being aborted */
    
    h2_io_ctx io;                     /* io on httpd conn filters */
    struct h2_bucket_queue *data_in;  /* stream data coming in */
    struct h2_bucket_queue *data_out; /* stream data going out */

    struct h2_stream_set *streams;  /* streams handled by this session */
    struct h2_stream_set *readies;  /* streams ready for submit */
    
    struct apr_thread_mutex_t *lock;
    struct apr_thread_cond_t *has_data; /* there is data to be written */
    
    on_new_task *on_new_task_cb;    /* notify of new h2_task creations */

    int loglvl;
    
    struct nghttp2_session *ngh2;   /* the nghttp2 session (internal use) */

} h2_session;


/* Create a new h2_session for the given connection that uses the
 * memory pool of that connection.
 * The session will allow the given maximum of concurrent streams.
 */
h2_session *h2_session_create(conn_rec *c, struct h2_config *cfg);

/* Destroy the session and all object it still contains. This will not
 * destroy h2_task instances that not finished yet. */
void h2_session_destroy(h2_session *session);

/* Called once at start of session. Performs initial client thingies. */
apr_status_t h2_session_start(h2_session *session);

/* Called when controlled shutdown is no longer an option. For 
 * example, when the client simply closed the connection. */
apr_status_t h2_session_abort(h2_session *session);

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
                                        struct h2_stream *stream);

/* Set the callback to be invoked when new h2_task instances are created.  */
void h2_session_set_new_task_cb(h2_session *session, on_new_task *callback);

/* Get the h2_stream for the given stream idenrtifier. */
struct h2_stream *h2_session_get_stream(h2_session *session, int stream_id);

/* Get the first h2_session that has a response ready and not submitted
 * yet. Returns NULL if no such session is available. Will only return
 * a stream once. */
struct h2_stream *h2_session_pop_ready_response(h2_session *session);

#endif /* defined(__mod_h2__h2_session__) */
