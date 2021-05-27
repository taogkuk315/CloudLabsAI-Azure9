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
    
    if (APR_SUCCESS == apr_thread_mutex_create(&q->lock,
                                               APR_THREAD_MUTEX_DEFAULT,
                                               pool)) {
        if (APR_SUCCESS == apr_thread_cond_create(&q->has_data, pool)) {
            return q;
        }
    }
    h2_bucket_queue_destroy(q);
    return NULL;
}

void h2_bucket_queue_destroy(h2_bucket_queue *q)
{
    if (q->lock) {
        apr_thread_mutex_destroy(q->lock);
        q->lock = NULL;
    }
    if (q->has_data) {
        apr_thread_cond_destroy(q->has_data);
        q->has_data = NULL;
    }
    if (q->queue) {
        h2_queue_destroy(q->queue);
        q->queue = NULL;
    }
}

void h2_bucket_queue_term(h2_bucket_queue *q)
{
    apr_status_t status = apr_thread_mutex_lock(q->lock);
    if (status == APR_SUCCESS) {
        h2_queue_term(q->queue);
        apr_thread_cond_broadcast(q->has_data);
        apr_thread_mutex_unlock(q->lock);
    }
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


static apr_status_t pop_int(h2_bucket_queue *q,
                                     apr_read_type_e block,
                                     int match_id,
                                     h2_bucket **pbucket,
                                     int *pstream_id)
{
    apr_status_t status = apr_thread_mutex_lock(q->lock);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    h2_bucket *bucket = h2_queue_pop_find(q->queue, find_first_for, &match_id);
    while (!bucket
           && block == APR_BLOCK_READ
           && !h2_queue_is_terminated(q->queue)) {
        apr_thread_cond_wait(q->has_data, q->lock);
        bucket = h2_queue_pop_find(q->queue, find_first_for, &match_id);
    }
    
    if (bucket == &H2_NULL_BUCKET) {
        *pbucket = NULL;
        status = APR_EOF;
    }
    else if (bucket) {
        *pbucket = bucket;
    }
    else if (block == APR_NONBLOCK_READ) {
        status = APR_EAGAIN;
    }
    else {
        status = APR_EOF;
    }
    
    apr_thread_mutex_unlock(q->lock);
    return status;
}

apr_status_t h2_bucket_queue_push(h2_bucket_queue *q, h2_bucket *bucket,
                                  int stream_id)
{
    apr_status_t status = apr_thread_mutex_lock(q->lock);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    if (q->queue->terminated) {
        status = APR_EOF;
    }
    else {
        if (q->ev_cb) {
            q->ev_cb(q, H2_BQ_EV_BEFORE_PUSH, bucket, stream_id, q->ev_ctx);
        }
        status = h2_queue_push_id(q->queue, stream_id, bucket);
        apr_thread_cond_broadcast(q->has_data);
    }
    apr_thread_mutex_unlock(q->lock);
    return status;
}

apr_status_t h2_bucket_queue_pop(h2_bucket_queue *q, apr_read_type_e block,
                                 int stream_id, h2_bucket **pbucket)
{
    int dummy;
    return pop_int(q, block, stream_id, pbucket, &dummy);
}

apr_status_t h2_bucket_queue_append(h2_bucket_queue *q,
                                    h2_bucket *bucket, int stream_id)
{
    apr_status_t status = apr_thread_mutex_lock(q->lock);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    if (q->queue->terminated) {
        status = APR_EOF;
    }
    else {
        if (q->ev_cb) {
            q->ev_cb(q, H2_BQ_EV_BEFORE_APPEND, bucket, stream_id, q->ev_ctx);
        }
        status = h2_queue_append_id(q->queue, stream_id, bucket);
        apr_thread_cond_broadcast(q->has_data);
    }
    apr_thread_mutex_unlock(q->lock);
    return status;
}

apr_status_t h2_bucket_queue_append_eos(h2_bucket_queue *q,
                                        int stream_id)
{
    return h2_bucket_queue_append(q, &H2_NULL_BUCKET, stream_id);
}

int h2_bucket_queue_has_eos_for(h2_bucket_queue *q, int stream_id)
{
    int eos_found = 0;
    apr_status_t status = apr_thread_mutex_lock(q->lock);
    if (status == APR_SUCCESS) {
        h2_bucket *b = h2_queue_find(q->queue, find_eos_for, (void*)&stream_id);
        eos_found = (b != NULL);
        apr_thread_mutex_unlock(q->lock);
    }
    
    return eos_found;
}

int h2_bucket_queue_is_empty(h2_bucket_queue *q)
{
    int empty = 0;
    apr_status_t status = apr_thread_mutex_lock(q->lock);
    if (status == APR_SUCCESS) {
        empty = h2_queue_is_empty(q->queue);
        apr_thread_mutex_unlock(q->lock);
    }
    
    return empty;
}

int h2_bucket_queue_has_buckets_for(h2_bucket_queue *q, int stream_id)
{
    int found = 0;
    apr_status_t status = apr_thread_mutex_lock(q->lock);
    if (status == APR_SUCCESS) {
        h2_bucket *b = h2_queue_find_id(q->queue, stream_id);
        found = (b != NULL);
        apr_thread_mutex_unlock(q->lock);
    }
    
    return found;
}

void h2_bucket_queue_set_event_cb(h2_bucket_queue *queue,
                                  h2_bucket_queue_event_cb *callback,
                                  void *ev_ctx)
{
    queue->ev_cb = callback;
    queue->ev_ctx = ev_ctx;
}

