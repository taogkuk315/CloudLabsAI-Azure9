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

#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_stream_task.h"
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

apr_status_t h2_task_set_add(h2_task_set *sp, h2_stream_task *task)
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

h2_stream_task *h2_task_set_get(h2_task_set *sp, int stream_id)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_stream_task *task = h2_queue_find_id(sp->queue, stream_id);
        apr_thread_mutex_unlock(sp->lock);
        return task;
    }
    return NULL;
}

h2_stream_task *h2_task_set_get_any(h2_task_set *sp)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_stream_task *task = h2_queue_pop(sp->queue);
        apr_thread_mutex_unlock(sp->lock);
        return task;
    }
    return NULL;
}

h2_stream_task *h2_task_set_remove(h2_task_set *sp, h2_stream_task *task)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_stream_task *task = h2_queue_remove(sp->queue, task);
        apr_thread_mutex_unlock(sp->lock);
        return task;
    }
    return NULL;
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
