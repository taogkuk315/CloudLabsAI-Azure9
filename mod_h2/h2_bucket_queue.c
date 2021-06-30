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

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>
#include <http_connection.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_queue.h"
#include "h2_bucket_queue.h"

static void bucket_free(void *entry)
{
    h2_bucket_destroy((h2_bucket *)entry);
}

h2_bucket_queue *h2_bucket_queue_create(apr_pool_t *pool)
{
    h2_bucket_queue *q = apr_pcalloc(pool, sizeof(h2_bucket_queue));
    if (!q) {
        return NULL;
    }
    q->queue = h2_queue_create(pool, bucket_free);
    if (!q->queue) {
        return NULL;
    }
    return q;
}

typedef struct {
    int stream_id;
    apr_size_t size;
} count_ctx;

int count_stream(void *puser, int id, void *entry, int index)
{
    count_ctx *ctx = (count_ctx*)puser;
    if (ctx->stream_id == id) {
        h2_bucket *bucket = (h2_bucket*)entry;
        ctx->size += bucket->data_len;
    }
    return 1;
}

apr_size_t h2_bucket_queue_get_stream_size(h2_bucket_queue *q, int stream_id) {
    count_ctx ctx = { stream_id, 0 };
    h2_queue_iter(q->queue, count_stream, &ctx);
    return ctx.size;
}

void h2_bucket_queue_destroy(h2_bucket_queue *q)
{
    if (q->queue) {
        h2_queue_destroy(q->queue);
        q->queue = NULL;
    }
}

void h2_bucket_queue_abort(h2_bucket_queue *q)
{
    h2_queue_abort(q->queue);
}

static void *find_eos_for(void *ctx, int id, void *entry)
{
    int match_id = *((int *)ctx);
    return ((id == match_id) && (entry == &H2_NULL_BUCKET))? entry : NULL;
}

static void *find_first_for(void *ctx, int id, void *entry)
{
    int match_id = *((int *)ctx);
    return (id == match_id || H2_QUEUE_ID_NONE == match_id)? entry : NULL;
}


apr_status_t h2_bucket_queue_push(h2_bucket_queue *q,
                                  int stream_id, h2_bucket *bucket)
{
    if (q->queue->aborted) {
        return APR_EOF;
    }
    
    return h2_queue_push_id(q->queue, stream_id, bucket);
}

apr_status_t h2_bucket_queue_pop(h2_bucket_queue *q,
                                 int stream_id, h2_bucket **pbucket)
{
    *pbucket = NULL;
    h2_bucket *bucket = h2_queue_pop_find(q->queue, find_first_for, &stream_id);
    if (bucket == &H2_NULL_BUCKET) {
        return APR_EOF;
    }
    else if (bucket) {
        *pbucket = bucket;
        return APR_SUCCESS;
    }
    return APR_EAGAIN;
}

apr_status_t h2_bucket_queue_append(h2_bucket_queue *q, int stream_id,
                                    h2_bucket *bucket)
{
    if (q->queue->aborted) {
        return APR_EOF;
    }
    
    return h2_queue_append_id(q->queue, stream_id, bucket);
}

apr_status_t h2_bucket_queue_append_eos(h2_bucket_queue *q,
                                        int stream_id)
{
    return h2_bucket_queue_append(q, stream_id, &H2_NULL_BUCKET);
}

typedef struct {
    h2_bucket_queue_iter_fn *cb;
    void *ctx;
} my_iter_ctx;

static int my_iter(void *ctx, int stream_id, void *entry, int index)
{
    my_iter_ctx *ictx = (my_iter_ctx *)ctx;
    return ictx->cb(ictx->ctx, stream_id, (h2_bucket *)entry, index);
}

void h2_bucket_queue_iter(h2_bucket_queue *q,
                          h2_bucket_queue_iter_fn *iter, void *ctx)
{
    my_iter_ctx ictx = { iter, ctx };
    h2_queue_iter(q->queue, my_iter, (void*)&ictx);
}

int h2_bucket_queue_has_eos_for(h2_bucket_queue *q, int stream_id)
{
    h2_bucket *b = h2_queue_find(q->queue, find_eos_for, (void*)&stream_id);
    return (b != NULL);
}

int h2_bucket_queue_is_empty(h2_bucket_queue *q)
{
    return h2_queue_is_empty(q->queue);
}

int h2_bucket_queue_has_buckets_for(h2_bucket_queue *q, int stream_id)
{
    h2_bucket *b = h2_queue_find_id(q->queue, stream_id);
    return (b != NULL);
}

