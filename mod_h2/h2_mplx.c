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
#include <http_log.h>

#include "h2_private.h"
#include "h2_queue.h"
#include "h2_bucket_queue.h"
#include "h2_resp_head.h"
#include "h2_mplx.h"

static void free_resp_head(void *p)
{
    h2_resp_head *head = (h2_resp_head *)p;
    h2_resp_head_destroy(head);
}

h2_mplx *h2_mplx_create(long id)
{
    apr_pool_t *pool = NULL;
    apr_status_t status = apr_pool_create_core(&pool);
    if (status != APR_SUCCESS) {
        return NULL;
    }
    
    h2_mplx *m = apr_pcalloc(pool, sizeof(h2_mplx));
    if (m) {
        m->id = id;
        m->pool = pool;
        m->ref_count = 1;
        
        m->heads = h2_queue_create(pool, free_resp_head);
        m->input = h2_bucket_queue_create(pool, 0);
        m->output = h2_bucket_queue_create(pool, 0);
        
        status = apr_thread_mutex_create(&m->lock, APR_THREAD_MUTEX_DEFAULT,
                                         pool);
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_create(&m->added_input, pool);
        }
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_create(&m->added_output, pool);
        }
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_create(&m->removed_output, pool);
        }
        
        if (status != APR_SUCCESS) {
            h2_mplx_destroy(m);
            return NULL;
        }
    }
    return m;
}

void h2_mplx_destroy(h2_mplx *m)
{
    if (m->heads) {
        h2_queue_destroy(m->heads);
        m->heads = NULL;
    }
    if (m->input) {
        h2_bucket_queue_destroy(m->input);
        m->input = NULL;
    }
    if (m->output) {
        h2_bucket_queue_destroy(m->output);
        m->output = NULL;
    }
    if (m->added_input) {
        apr_thread_cond_destroy(m->added_input);
        m->added_input = NULL;
    }
    if (m->added_output) {
        apr_thread_cond_destroy(m->added_output);
        m->added_output = NULL;
    }
    if (m->removed_output) {
        apr_thread_cond_destroy(m->removed_output);
        m->removed_output = NULL;
    }
    if (m->lock) {
        apr_thread_mutex_destroy(m->lock);
        m->lock = NULL;
    }
    if (m->pool) {
        apr_pool_destroy(m->pool);
        /* all our memory is gone, like tears in the rain */
    }
}

apr_status_t h2_mplx_reference(h2_mplx *m)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        ++m->ref_count;
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_release(h2_mplx *m)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        if (--m->ref_count <= 0) {
            h2_mplx_destroy(m);
            return APR_SUCCESS;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

void h2_mplx_abort(h2_mplx *m)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        m->aborted = 1;
        h2_queue_abort(m->heads);
        h2_bucket_queue_abort(m->input);
        h2_bucket_queue_abort(m->output);
        apr_thread_mutex_unlock(m->lock);
    }
}

apr_status_t h2_mplx_in_read(h2_mplx *m, apr_read_type_e block,
                             int channel, struct h2_bucket **pbucket)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_pop(m->input, channel, pbucket);
        while (block == APR_BLOCK_READ && status == APR_EAGAIN) {
            apr_thread_cond_wait(m->added_input, m->lock);
            status = h2_bucket_queue_pop(m->input, channel, pbucket);
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_in_write(h2_mplx *m,
                              int channel, struct h2_bucket *bucket)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_append(m->input, channel, bucket);
        apr_thread_cond_broadcast(m->added_input);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_in_close(h2_mplx *m, int channel)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_append_eos(m->input, channel);
        apr_thread_cond_broadcast(m->added_input);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_read(h2_mplx *m,
                              int channel, struct h2_bucket **pbucket)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_pop(m->output, channel, pbucket);
        apr_thread_cond_broadcast(m->removed_output);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_pushback(h2_mplx *m, int channel,
                                  struct h2_bucket *bucket)

{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_push(m->output, channel, bucket);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_open(h2_mplx *m, int channel, h2_resp_head *head)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_queue_append(m->heads, head);
        apr_thread_cond_broadcast(m->added_output);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

h2_resp_head *h2_mplx_pop_response(h2_mplx *m)
{
    h2_resp_head *head = NULL;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        head = (h2_resp_head*)h2_queue_pop(m->heads);
        apr_thread_mutex_unlock(m->lock);
    }
    return head;
}

apr_status_t h2_mplx_out_write(h2_mplx *m, apr_read_type_e block,
                               int channel, struct h2_bucket *bucket)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        // TODO: check max queue size and block if necessary
        status = h2_bucket_queue_append(m->output, channel, bucket);
        while (status == APR_EAGAIN && block == APR_BLOCK_READ) {
            apr_thread_cond_wait(m->removed_output, m->lock);
            status = h2_bucket_queue_append(m->output, channel, bucket);
        }
        if (status == APR_SUCCESS) {
            apr_thread_cond_broadcast(m->added_output);
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_close(h2_mplx *m, int channel)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_append_eos(m->output, channel);
        apr_thread_cond_broadcast(m->added_output);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

int h2_mplx_in_has_eos_for(h2_mplx *m, int channel)
{
    int has_eos = 0;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        has_eos = h2_bucket_queue_has_eos_for(m->input, channel);
        apr_thread_mutex_unlock(m->lock);
    }
    return has_eos;
}

int h2_mplx_out_has_data_for(h2_mplx *m, int channel)
{
    int has_data = 0;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        has_data = h2_bucket_queue_has_buckets_for(m->input, channel);
        apr_thread_mutex_unlock(m->lock);
    }
    return has_data;
}

apr_status_t h2_mplx_out_trywait(h2_mplx *m, apr_interval_time_t timeout)
{
    int has_data = 0;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = apr_thread_cond_timedwait(m->added_output, m->lock, timeout);
        apr_thread_mutex_unlock(m->lock);
    }
    return has_data;
}

