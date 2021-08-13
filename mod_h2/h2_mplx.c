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
#include <stddef.h>

#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>
#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_config.h"
#include "h2_queue.h"
#include "h2_bucket.h"
#include "h2_bucket_queue.h"
#include "h2_response.h"
#include "h2_mplx.h"

struct h2_mplx {
    long id;
    apr_pool_t *pool;
    
    h2_queue *responses;
    h2_bucket_queue *input;
    h2_bucket_queue *output;
    
    apr_thread_mutex_t *lock;
    apr_thread_cond_t *added_input;
    apr_thread_cond_t *added_output;
    apr_thread_cond_t *removed_output;
    
    int ref_count;
    int aborted;
    
    apr_size_t out_stream_max_size;
};

static void free_response(void *p)
{
    h2_response *head = (h2_response *)p;
    h2_response_destroy(head);
}

static int is_aborted(h2_mplx *m, apr_status_t *pstatus) {
    if (m->aborted) {
        *pstatus = APR_ECONNABORTED;
        return 1;
    }
    return 0;
}

static void have_in_data_for(h2_mplx *m, int stream_id);
static void have_out_data_for(h2_mplx *m, int stream_id);
static void consumed_out_data_for(h2_mplx *m, int stream_id);

h2_mplx *h2_mplx_create(long id, apr_pool_t *master, h2_config *conf)
{
    h2_mplx *m = apr_pcalloc(master, sizeof(h2_mplx));
    if (m) {
        m->id = id;
        m->pool = master;
        m->ref_count = 1;
        
        assert(conf);
        
        m->responses = h2_queue_create(m->pool, free_response);
        
        m->input = h2_bucket_queue_create(m->pool);
        m->out_stream_max_size =
            h2_config_geti(conf, H2_CONF_STREAM_MAX_MEM_SIZE);
        m->output = h2_bucket_queue_create(m->pool);
        
        apr_status_t status = apr_thread_mutex_create(&m->lock,
                                                      APR_THREAD_MUTEX_DEFAULT,
                                                      m->pool);
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_create(&m->added_input, m->pool);
        }
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_create(&m->added_output, m->pool);
        }
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_create(&m->removed_output, m->pool);
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
    if (m->responses) {
        h2_queue_destroy(m->responses);
        m->responses = NULL;
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
}

apr_pool_t *h2_mplx_get_pool(h2_mplx *m)
{
    return m->pool;
}

long h2_mplx_get_id(h2_mplx *m)
{
    return m->id;
}

void h2_mplx_abort(h2_mplx *m)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        m->aborted = 1;
        h2_queue_abort(m->responses);
        h2_bucket_queue_abort(m->input);
        h2_bucket_queue_abort(m->output);
        apr_thread_mutex_unlock(m->lock);
    }
}

apr_status_t h2_mplx_in_read(h2_mplx *m, apr_read_type_e block,
                             int stream_id, struct h2_bucket **pbucket)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_pop(m->input, stream_id, pbucket);
        while (!is_aborted(m, &status)
               && block == APR_BLOCK_READ && status == APR_EAGAIN) {
            apr_thread_cond_wait(m->added_input, m->lock);
            status = h2_bucket_queue_pop(m->input, stream_id, pbucket);
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_in_write(h2_mplx *m,
                              int stream_id, struct h2_bucket *bucket)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_append(m->input, stream_id, bucket);
        have_in_data_for(m, stream_id);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_in_close(h2_mplx *m, int stream_id)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_append_eos(m->input, stream_id);
        have_in_data_for(m, stream_id);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_read(h2_mplx *m,
                              int stream_id, struct h2_bucket **pbucket)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_pop(m->output, stream_id, pbucket);
        ap_log_perror(APLOG_MARK, APLOG_DEBUG, status, m->pool,
                      "h2_mplx(%ld): read on stream_id-out(%d)",
                      m->id, stream_id);
        if (status == APR_SUCCESS) {
            consumed_out_data_for(m, stream_id);
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_pushback(h2_mplx *m, int stream_id,
                                  struct h2_bucket *bucket)

{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_push(m->output, stream_id, bucket);
        ap_log_perror(APLOG_MARK, APLOG_DEBUG, status, m->pool,
                      "h2_mplx(%ld): pushback on stream_id-out(%d)",
                      m->id, stream_id);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_open(h2_mplx *m, int stream_id, h2_response *response)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_queue_append_id(m->responses, stream_id, response);
        ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, m->pool,
                      "h2_mplx(%ld): open on stream_id-in(%d)",
                      m->id, stream_id);
        have_out_data_for(m, stream_id);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_reset(h2_mplx *m, int stream_id, apr_status_t ss)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_queue_append_id(m->responses, stream_id,
                           h2_response_create(stream_id, ss,
                                              NULL, NULL, NULL,
                                              m->pool));
        have_out_data_for(m, stream_id);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

h2_response *h2_mplx_pop_response(h2_mplx *m)
{
    h2_response *head = NULL;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        head = (h2_response*)h2_queue_pop(m->responses);
        if (head) {
            ap_log_perror(APLOG_MARK, APLOG_DEBUG, status, m->pool,
                          "h2_mplx(%ld): popped response(%d)",
                          m->id, head->stream_id);
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return head;
}

apr_status_t h2_mplx_out_write(h2_mplx *m, apr_read_type_e block,
                               int stream_id, struct h2_bucket *bucket)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        /* We check the memory footprint queued for this stream_id
         * and block if it exceeds our configured limit.
         * We will not split buckets to enforce the limit to the last
         * byte. After all, the bucket is already in memory.
         */
        while (!is_aborted(m, &status)
               && (m->out_stream_max_size
                   < h2_bucket_queue_get_stream_size(m->output, stream_id))) {
            ap_log_perror(APLOG_MARK, APLOG_DEBUG, status, m->pool,
                          "h2_mplx(%ld-%d): blocking on queue size",
                          m->id, stream_id);
            apr_thread_cond_wait(m->removed_output, m->lock);
        }
        
        if (!is_aborted(m, &status)) {
            status = h2_bucket_queue_append(m->output, stream_id, bucket);
            ap_log_perror(APLOG_MARK, APLOG_DEBUG, status, m->pool,
                          "h2_mplx(%ld): write %ld bytes on stream_id-out(%d)",
                          m->id, bucket->data_len, stream_id);
            if (status == APR_SUCCESS) {
                have_out_data_for(m, stream_id);
            }
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_close(h2_mplx *m, int stream_id)
{
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = h2_bucket_queue_append_eos(m->output, stream_id);
        ap_log_perror(APLOG_MARK, APLOG_DEBUG, status, m->pool,
                      "h2_mplx(%ld): close stream_id-out(%d)",
                      m->id, stream_id);
        have_out_data_for(m, stream_id);
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

int h2_mplx_in_has_eos_for(h2_mplx *m, int stream_id)
{
    int has_eos = 0;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        has_eos = h2_bucket_queue_has_eos_for(m->input, stream_id);
        apr_thread_mutex_unlock(m->lock);
    }
    return has_eos;
}

int h2_mplx_out_has_data_for(h2_mplx *m, int stream_id)
{
    int has_data = 0;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        has_data = h2_bucket_queue_has_buckets_for(m->output, stream_id);
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
        ap_log_perror(APLOG_MARK, APLOG_DEBUG, status, m->pool,
                      "h2_mplx(%ld): trywait on data for %f ms)",
                      m->id, timeout/1000.0);
        apr_thread_mutex_unlock(m->lock);
    }
    return has_data;
}

static void have_in_data_for(h2_mplx *m, int stream_id)
{
    apr_thread_cond_broadcast(m->added_input);
}

static void have_out_data_for(h2_mplx *m, int stream_id)
{
    apr_thread_cond_broadcast(m->added_output);
}

static void consumed_out_data_for(h2_mplx *m, int stream_id)
{
    apr_thread_cond_broadcast(m->removed_output);
}

