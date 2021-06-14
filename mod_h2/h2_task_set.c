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

#include <stddef.h>

#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>
#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_queue.h"
#include "h2_session.h"
#include "h2_task.h"
#include "h2_task.h"
#include "h2_task_set.h"

h2_task_set *h2_task_set_create(apr_pool_t *pool)
{
    h2_task_set *sp = apr_pcalloc(pool, sizeof(h2_task_set));
    if (!sp) {
        return NULL;
    }
    
    sp->queue = h2_queue_create(pool, NULL);
    if (!sp->queue) {
        return NULL;
    }
    
    if (APR_SUCCESS == apr_thread_mutex_create(&sp->lock,
                                               APR_THREAD_MUTEX_DEFAULT,
                                               pool)) {
        return sp;
    }
    h2_task_set_destroy(sp);
    return NULL;
}

void h2_task_set_destroy(h2_task_set *sp)
{
    if (sp->lock) {
        apr_thread_mutex_destroy(sp->lock);
        sp->lock = NULL;
    }
    if (sp->queue) {
        h2_queue_destroy(sp->queue);
        sp->queue = NULL;
    }
}

apr_status_t h2_task_set_term(h2_task_set *sp)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_queue_term(sp->queue);
        apr_thread_mutex_unlock(sp->lock);
    }
    return status;
}

apr_status_t h2_task_set_add(h2_task_set *sp, h2_task *task)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        if (!h2_queue_find_id(sp->queue, task->stream_id)) {
            h2_queue_push_id(sp->queue, task->stream_id, task);
        }
        apr_thread_mutex_unlock(sp->lock);
    }
    return status;
}

h2_task *h2_task_set_get(h2_task_set *sp, int stream_id)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_task *stream = h2_queue_find_id(sp->queue, stream_id);
        apr_thread_mutex_unlock(sp->lock);
        return stream;
    }
    return NULL;
}

h2_task *h2_task_set_remove(h2_task_set *sp, h2_task *stream)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_task *s = h2_queue_remove(sp->queue, stream);
        apr_thread_mutex_unlock(sp->lock);
        return stream;
    }
    return NULL;
}

void h2_task_set_remove_all(h2_task_set *sp)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_queue_remove_all(sp->queue);
        apr_thread_mutex_unlock(sp->lock);
    }
}

void h2_task_set_abort_all(h2_task_set *sp)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_task *task;
        while ((task = h2_queue_pop(sp->queue)) != NULL) {
            h2_task_abort(task);
        }
        apr_thread_mutex_unlock(sp->lock);
    }
}

void h2_task_set_destroy_all(h2_task_set *sp)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_task *task;
        while ((task = h2_queue_pop(sp->queue)) != NULL) {
            h2_task_destroy(task);
        }
        apr_thread_mutex_unlock(sp->lock);
    }
}


int h2_task_set_is_empty(h2_task_set *sp)
{
    int empty = 0;
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        empty = h2_queue_is_empty(sp->queue);
        apr_thread_mutex_unlock(sp->lock);
    }
    return empty;
}

typedef struct {
    h2_task_set_match_fn match;
    void *ctx;
} h2_task_match_ctx;

static void *find_match(void *ctx, int id, void *entry)
{
    h2_task_match_ctx *mctx = (h2_task_match_ctx*)ctx;
    return mctx->match(mctx->ctx, (h2_task *)entry);
}

h2_task *h2_task_set_find(h2_task_set *sp,
                              h2_task_set_match_fn match, void *ctx)
{
    h2_task *stream = NULL;
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_task_match_ctx mctx = { match, ctx };
        stream = h2_queue_find(sp->queue, find_match, &mctx);
        apr_thread_mutex_unlock(sp->lock);
    }
    return stream;
}


