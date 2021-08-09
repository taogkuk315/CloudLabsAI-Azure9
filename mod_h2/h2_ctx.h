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

#ifndef __mod_h2__h2_ctx__
#define __mod_h2__h2_ctx__

struct h2_task;

/**
 * The h2 module context associated with a connection. 
 *
 * It keeps track of the different types of connections:
 * - those from clients that use HTTP/2 protocol
 * - those from clients that do not use HTTP/2
 * - those created by ourself to perform work on HTTP/2 streams
 */
typedef struct h2_ctx {
    int is_h2;                /* h2 engine is used */
    const char *protocol;     /* the protocol negotiated */
    int is_negotiated;        /* negotiated did happen */
    struct h2_task *task;     /* the h2_task or NULL */
} h2_ctx;

h2_ctx *h2_ctx_create(conn_rec *c);
h2_ctx *h2_ctx_create_for(conn_rec *c, struct h2_task *task);
h2_ctx *h2_ctx_get(conn_rec *c);

const char *h2_ctx_get_protocol(conn_rec* c);
h2_ctx *h2_ctx_set_protocol(conn_rec* c, const char *proto);
int h2_ctx_is_negotiated(conn_rec * c);

int h2_ctx_is_session(conn_rec * c);
int h2_ctx_is_task(conn_rec * c);
int h2_ctx_is_active(conn_rec * c);

struct h2_task *h2_ctx_get_task(h2_ctx *ctx);

#endif /* defined(__mod_h2__h2_ctx__) */
