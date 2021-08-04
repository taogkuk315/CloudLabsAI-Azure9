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
#include <stddef.h>

#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_queue.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_task.h"
#include "h2_stream_set.h"


struct h2_stream_set {
    h2_queue *queue;
};

h2_stream_set *h2_stream_set_create(apr_pool_t *pool)
{
    h2_stream_set *sp = apr_pcalloc(pool, sizeof(h2_stream_set));
    if (sp) {
        sp->queue = h2_queue_create(pool, NULL);
        if (!sp->queue) {
            return NULL;
        }
    }
    return sp;
}

void h2_stream_set_destroy(h2_stream_set *sp)
{
    if (sp->queue) {
        h2_queue_destroy(sp->queue);
        sp->queue = NULL;
    }
}

void h2_stream_set_term(h2_stream_set *sp)
{
    h2_queue_abort(sp->queue);
}

apr_status_t h2_stream_set_add(h2_stream_set *sp, h2_stream *stream)
{
    if (h2_stream_set_get(sp, stream->id) == NULL) {
        return h2_queue_push_id(sp->queue, stream->id, stream);
    }
    return APR_SUCCESS;
}

h2_stream *h2_stream_set_get(h2_stream_set *sp, int stream_id)
{
    return (h2_stream *)h2_queue_find_id(sp->queue, stream_id);
}

h2_stream *h2_stream_set_remove(h2_stream_set *sp, h2_stream *stream)
{
    return (h2_stream *)h2_queue_remove(sp->queue, stream);
}

void h2_stream_set_remove_all(h2_stream_set *sp)
{
    h2_queue_remove_all(sp->queue);
}

int h2_stream_set_is_empty(h2_stream_set *sp)
{
    assert(sp);
    assert(sp->queue);
    return h2_queue_is_empty(sp->queue);
}

typedef struct {
    h2_stream_set_match_fn *match;
    void *ctx;
} h2_stream_match_ctx;

static void *find_match(void *ctx, int id, void *entry)
{
    h2_stream_match_ctx *mctx = (h2_stream_match_ctx*)ctx;
    return mctx->match(mctx->ctx, (h2_stream *)entry);
}

h2_stream *h2_stream_set_find(h2_stream_set *sp,
                              h2_stream_set_match_fn match, void *ctx)
{
    h2_stream_match_ctx mctx = { match, ctx };
    return h2_queue_find(sp->queue, find_match, &mctx);
}

typedef struct {
    h2_stream_set_iter_fn *iter;
    void *ctx;
} h2_stream_iter_ctx;

static int iter_wrap(void *ctx, int id, void *entry, int index)
{
    h2_stream_iter_ctx *ictx = (h2_stream_iter_ctx*)ctx;
    return ictx->iter(ictx->ctx, (h2_stream *)entry);
}

void h2_stream_set_iter(h2_stream_set *sp,
                        h2_stream_set_iter_fn *iter, void *ctx)
{
    h2_stream_iter_ctx ictx = { iter, ctx };
    h2_queue_iter(sp->queue, iter_wrap, &ictx);
}

apr_size_t h2_stream_set_size(h2_stream_set *sp)
{
    return h2_queue_size(sp->queue);
}

