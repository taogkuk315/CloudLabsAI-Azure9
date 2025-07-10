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
struct h2_config;

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
    const char *protocol;           /* the protocol negotiated */
    server_rec *server;             /* httpd server selected. */
    struct h2_session *session;     /* on main: the session established */
    struct h2_mplx *mplx;           /* on secondary: the multiplexer */
    struct h2_task *task;           /* on secondary: the task processed */
    const struct h2_config *config; /* effective config in this context */
};
typedef struct h2_conn_ctx_t h2_conn_ctx_t;

/**
 * Get the h2 connection context.
 * @param c the connection to look at
 * @return h2 context of this connection
 */
#define h2_conn_ctx_get(c) \
    ((h2_conn_ctx_t*)ap_get_module_config((c)->conn_config, &http2_module))

/**
 * Create the h2 connection context.
 * @param c the connection to create it at
 * @return created h2 context of this connection
 */
h2_conn_ctx_t *h2_conn_ctx_create(const conn_rec *c);

h2_conn_ctx_t *h2_conn_ctx_create_secondary(const conn_rec *c, struct h2_stream *stream);

void h2_conn_ctx_clear(const conn_rec *c);

/**
 * Get the session instance if `c` is a HTTP/2 master connection.
 */
struct h2_session *h2_conn_ctx_get_session(conn_rec *c);

/**
 * Get the h2_task instance of `c` is a HTTP/2 secondary connection.
 */
struct h2_task *h2_conn_ctx_get_task(conn_rec *c);

#endif /* defined(__mod_h2__h2_ctx__) */
