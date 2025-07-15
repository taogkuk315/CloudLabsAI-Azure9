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

#ifndef __mod_h2__h2_ctx__
#define __mod_h2__h2_ctx__

struct h2_session;
struct h2_stream;
struct h2_mplx;
struct h2_task;
struct h2_bucket_beam;

/**
 * The h2 module context associated with a connection. 
 *
 * It keeps track of the different types of connections:
 * - those from clients that use HTTP/2 protocol
 * - those from clients that do not use HTTP/2
 * - those created by ourself to perform work on HTTP/2 streams
 */
struct h2_conn_ctx_t {
    const char *id;                 /* our identifier of this connection */
    apr_pool_t *pool;               /* main: session pool, secondary: task pool */
    server_rec *server;             /* httpd server selected. */
    const char *protocol;           /* the protocol negotiated */
    struct h2_session *session;     /* on main: the session established */

    struct h2_mplx *mplx;           /* on secondary: the multiplexer */
    int stream_id;                  /* on main: 0, on secondary: stream id */
    const struct h2_request *request; /* on secondary: the request to process */

    int filters_set;                 /* protocol filters have been set up */
    int has_final_response;          /* request has produced a >= 200 response */
    int registered_at_mplx;          /* output is registered at mplx for polling */
    int out_unbuffered;              /* output is unbuffered */

    struct h2_bucket_beam *beam_in;
    struct h2_bucket_beam *beam_out;
    apr_bucket_brigade *bb_in;

    volatile int done;               /* processing has finished */
    apr_time_t started_at;           /* when processing started */
    apr_time_t done_at;              /* when processing was done */
};
typedef struct h2_conn_ctx_t h2_conn_ctx_t;

/**
 * Get the h2 connection context.
 * @param c the connection to look at
 * @return h2 context of this connection
 */
#define h2_conn_ctx_get(c) \
    ((c)? (h2_conn_ctx_t*)ap_get_module_config((c)->conn_config, &http2_module) : NULL)

/**
 * Create the h2 connection context.
 * @param c the connection to create it at
 * @return created h2 context of this connection
 */
h2_conn_ctx_t *h2_conn_ctx_create(conn_rec *c);

h2_conn_ctx_t *h2_conn_ctx_create_secondary(conn_rec *c, struct h2_stream *stream);

void h2_conn_ctx_detach(conn_rec *c);

/**
 * Distach from the connection and destroy all resources, e.g. the pool.
 */
void h2_conn_ctx_destroy(h2_conn_ctx_t *conn_ctx);

/**
 * Get the session instance if `c` is a HTTP/2 master connection.
 */
struct h2_session *h2_conn_ctx_get_session(conn_rec *c);

#endif /* defined(__mod_h2__h2_ctx__) */
