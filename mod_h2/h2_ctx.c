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

#include <assert.h>

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>

#include "h2_private.h"
#include "h2_bucket_queue.h"
#include "h2_task.h"
#include "h2_ctx.h"
#include "h2_private.h"

h2_ctx *h2_ctx_create(conn_rec *c)
{
    h2_ctx *ctx = apr_pcalloc(c->pool, sizeof(h2_ctx));
    assert(ctx);
    ap_set_module_config(c->conn_config, &h2_module, ctx);
    return ctx;
}

h2_ctx *h2_ctx_create_for(conn_rec *c, h2_task *task)
{
    h2_ctx *ctx = h2_ctx_create(c);
    if (ctx) {
        ctx->task = task;
    }
    return ctx;
}

h2_ctx *h2_ctx_get( conn_rec *c )
{
    return (h2_ctx*)ap_get_module_config(c->conn_config, &h2_module);
}

const char *h2_ctx_get_protocol( conn_rec* c )
{
    h2_ctx *ctx = h2_ctx_get(c);
    return ctx? ctx->protocol : NULL;
}

h2_ctx *h2_ctx_set_protocol(conn_rec* c, const char *proto)
{
    h2_ctx *ctx = h2_ctx_get(c);
    if (ctx) {
        ctx->protocol = proto;
        ctx->is_h2 = (proto != NULL);
        ctx->is_negotiated = 1;
    }
    return ctx;
}

int h2_ctx_is_session(conn_rec * c)
{
    h2_ctx *ctx = h2_ctx_get(c);
    return ctx && !ctx->task;
}

int h2_ctx_is_task(conn_rec * c)
{
    h2_ctx *ctx = h2_ctx_get(c);
    return ctx && !!ctx->task;
}

int h2_ctx_is_negotiated( conn_rec * c )
{
    h2_ctx *ctx = h2_ctx_get(c);
    return ctx && ctx->is_negotiated;
}

int h2_ctx_is_active(conn_rec * c)
{
    h2_ctx *ctx = h2_ctx_get(c);
    return ctx && ctx->protocol != NULL;
}

struct h2_task *h2_ctx_get_task(h2_ctx *ctx)
{
    return ctx->task;
}
