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
#include "h2_stream_pool.h"

h2_stream_pool *h2_stream_pool_create(apr_pool_t *pool)
{
    h2_stream_pool *sp = apr_pcalloc(pool, sizeof(h2_stream_pool));
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
    h2_stream_pool_destroy(sp);
    return NULL;
}

void h2_stream_pool_destroy(h2_stream_pool *sp)
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

apr_status_t h2_stream_pool_term(h2_stream_pool *sp)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_queue_term(sp->queue);
        apr_thread_mutex_unlock(sp->lock);
    }
    return status;
}

apr_status_t h2_stream_pool_add(h2_stream_pool *sp, h2_stream *stream)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_queue_push_id(sp->queue, stream->id, stream);
        apr_thread_mutex_unlock(sp->lock);
    }
    return status;
}

h2_stream *h2_stream_pool_get(h2_stream_pool *sp, int stream_id)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_stream *stream = h2_queue_find_id(sp->queue, stream_id);
        apr_thread_mutex_unlock(sp->lock);
        return stream;
    }
    return NULL;
}

h2_stream *h2_stream_pool_get_any(h2_stream_pool *sp)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_stream *stream = h2_queue_pop(sp->queue);
        apr_thread_mutex_unlock(sp->lock);
        return stream;
    }
    return NULL;
}

h2_stream *h2_stream_pool_remove(h2_stream_pool *sp, h2_stream *stream)
{
    apr_status_t status = apr_thread_mutex_lock(sp->lock);
    if (status == APR_SUCCESS) {
        h2_stream *stream = h2_queue_remove(sp->queue, stream);
        apr_thread_mutex_unlock(sp->lock);
        return stream;
    }
    return NULL;
}
