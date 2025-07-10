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
 
#include <assert.h>
#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>

#include "h2_private.h"
#include "h2_session.h"
#include "h2_task.h"
#include "h2_stream.h"
#include "h2_ctx.h"


static h2_conn_ctx_t *ctx_create(const conn_rec *c, const char *id)
{
    h2_conn_ctx_t *ctx = apr_pcalloc(c->pool, sizeof(*ctx));
    ctx->id = id;
    ctx->server = c->base_server;
    ap_set_module_config(c->conn_config, &http2_module, ctx);
    return ctx;
}

h2_conn_ctx_t *h2_conn_ctx_create(const conn_rec *c)
{
    return ctx_create(c, apr_psprintf(c->pool, "%ld", c->id));
}

h2_conn_ctx_t *h2_conn_ctx_create_secondary(const conn_rec *c, struct h2_stream *stream)
{
    /* there is sth fishy going on in some mpms that change the id of
     * a connection when they process it in another thread. stick to
     * the id the session was initialized with. */
    h2_conn_ctx_t *ctx = ctx_create(c, apr_psprintf(
        c->pool, "%ld-%d", stream->session->id, stream->id));
    ctx->mplx = stream->session->mplx;
    return ctx;
}

void h2_conn_ctx_clear(const conn_rec *c)
{
    ap_assert(c);
    ap_set_module_config(c->conn_config, &http2_module, NULL);
}

h2_session *h2_conn_ctx_get_session(conn_rec *c)
{
    h2_conn_ctx_t *ctx = (c && !c->master)? h2_conn_ctx_get(c) : NULL;
    return ctx? ctx->session : NULL;
}

h2_task *h2_conn_ctx_get_task(conn_rec *c)
{
    h2_conn_ctx_t *ctx = (c && c->master)? h2_conn_ctx_get(c) : NULL;
    return ctx? ctx->task : NULL;
}

