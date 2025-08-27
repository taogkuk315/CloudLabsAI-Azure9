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
#include <http_log.h>

#include "h2_private.h"
#include "h2_session.h"
#include "h2_bucket_beam.h"
#include "h2_c2.h"
#include "h2_stream.h"
#include "h2_conn_ctx.h"


void h2_conn_ctx_detach(conn_rec *c)
{
    h2_conn_ctx_t *conn_ctx = h2_conn_ctx_get(c);

    if (conn_ctx && conn_ctx->beam_out) {
        H2_BEAM_LOG(conn_ctx->beam_out, c, APLOG_TRACE2, "c2_destroy");
        h2_beam_destroy(conn_ctx->beam_out);
        conn_ctx->beam_out = NULL;
    }
    ap_set_module_config(c->conn_config, &http2_module, NULL);
}

static h2_conn_ctx_t *ctx_create(apr_pool_t *pool, conn_rec *c, const char *id)
{
    h2_conn_ctx_t *conn_ctx = apr_pcalloc(pool, sizeof(*conn_ctx));
    conn_ctx->id = id;
    conn_ctx->pool = pool;
    conn_ctx->server = c->base_server;
    conn_ctx->started_at = apr_time_now();
    ap_set_module_config(c->conn_config, &http2_module, conn_ctx);
    return conn_ctx;
}

void h2_conn_ctx_destroy(h2_conn_ctx_t *conn_ctx)
{
    apr_pool_destroy(conn_ctx->pool);
}

h2_conn_ctx_t *h2_conn_ctx_create_for_c1(conn_rec *c, server_rec *s, const char *protocol)
{
    h2_conn_ctx_t *ctx;
    ctx = ctx_create(c->pool, c, apr_psprintf(c->pool, "%ld", c->id));
    ctx->server = s;
    ctx->protocol = apr_pstrdup(c->pool, protocol);
    return ctx;
}

h2_conn_ctx_t *h2_conn_ctx_create_for_c2(conn_rec *c, struct h2_stream *stream)
{
    const char *id;
    h2_conn_ctx_t *ctx;
    apr_pool_t *pool;

    ap_assert(c->master);
    apr_pool_create(&pool, c->pool);
    apr_pool_tag(pool, "h2_c2");
    id = apr_psprintf(pool, "%ld-%d", stream->session->id, stream->id);
    ctx = ctx_create(pool, c, id);
    ctx->server = c->master->base_server; /* will be overridden once request_rec exists */
    ctx->mplx = stream->session->mplx;
    ctx->stream_id = stream->id;
    ctx->request = stream->request;

    return ctx;
}

h2_session *h2_conn_ctx_get_session(conn_rec *c)
{
    h2_conn_ctx_t *ctx = (c && !c->master)? h2_conn_ctx_get(c) : NULL;
    return ctx? ctx->session : NULL;
}

