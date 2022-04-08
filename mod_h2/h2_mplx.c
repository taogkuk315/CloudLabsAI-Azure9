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
#include "h2_bucket.h"
#include "h2_conn.h"
#include "h2_io.h"
#include "h2_io_set.h"
#include "h2_response.h"
#include "h2_mplx.h"
#include "h2_task.h"
#include "h2_task_input.h"
#include "h2_task_output.h"

struct h2_mplx {
    long id;
    conn_rec *c;
    apr_pool_t *pool;
    apr_bucket_alloc_t *bucket_alloc;
    
    h2_io_set *stream_ios;
    h2_io_set *ready_ios;
    h2_io_set *task_finished_ios;
    
    apr_thread_mutex_t *memlock;
    apr_thread_mutex_t *lock;
    apr_thread_cond_t *added_output;
    
    int aborted;
    
    apr_size_t out_stream_max_size;
};

static void free_response(void *p)
{
    h2_response *head = (h2_response *)p;
    h2_response_destroy(head);
}

static int is_aborted(h2_mplx *m, apr_status_t *pstatus) {
    assert(m);
    if (m->aborted) {
        *pstatus = APR_ECONNABORTED;
        return 1;
    }
    return 0;
}

static void have_out_data_for(h2_mplx *m, int stream_id);

h2_mplx *h2_mplx_create(conn_rec *c, apr_pool_t *parent)
{
    apr_status_t status = APR_SUCCESS;
    h2_config *conf = h2_config_get(c);
    assert(conf);
    
    apr_allocator_t *allocator = NULL;
    status = apr_allocator_create(&allocator);
    if (status != APR_SUCCESS) {
        return NULL;
    }

    h2_mplx *m = apr_pcalloc(parent, sizeof(h2_mplx));
    if (m) {
        m->id = c->id;
        m->c = c;
        apr_pool_create_ex(&m->pool, parent, NULL, allocator);
        if (!m->pool) {
            return NULL;
        }
        
        status = apr_thread_mutex_create(&m->memlock, 
                                         APR_THREAD_MUTEX_DEFAULT,
                                         m->pool);
        if (status != APR_SUCCESS) {
            h2_mplx_destroy(m);
            return NULL;
        }
        apr_allocator_mutex_set(allocator, m->memlock);
        

        m->bucket_alloc = apr_bucket_alloc_create(m->pool);
        
        m->stream_ios = h2_io_set_create(m->pool);
        m->ready_ios = h2_io_set_create(m->pool);
        m->task_finished_ios = h2_io_set_create(m->pool);
        m->out_stream_max_size =
            h2_config_geti(conf, H2_CONF_STREAM_MAX_MEM_SIZE);
        
        status = apr_thread_mutex_create(&m->lock, APR_THREAD_MUTEX_DEFAULT,
                                         m->pool);
        if (status != APR_SUCCESS) {
            h2_mplx_destroy(m);
            return NULL;
        }
        if (status != APR_SUCCESS) {
            return NULL;
        }
        
    }
    return m;
}

void h2_mplx_destroy(h2_mplx *m)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        m->aborted = 1;
        if (m->task_finished_ios) {
            h2_io_set_destroy(m->task_finished_ios);
            m->task_finished_ios = NULL;
        }
        if (m->ready_ios) {
            h2_io_set_destroy(m->ready_ios);
            m->ready_ios = NULL;
        }
        if (m->stream_ios) {
            h2_io_set_destroy(m->stream_ios);
            m->stream_ios = NULL;
        }
        apr_thread_mutex_unlock(m->lock);
        
        if (m->lock) {
            apr_thread_mutex_destroy(m->lock);
            m->lock = NULL;
        }
    }
    
    if (m->pool) {
        apr_allocator_t *allocator = apr_pool_allocator_get(m->pool);
        if (m->memlock) {
            if (allocator) {
                apr_allocator_mutex_set(allocator, NULL);
            }
            apr_thread_mutex_destroy(m->memlock);
            m->memlock = NULL;
        }
        apr_pool_destroy(m->pool);
        if (allocator) {
            apr_allocator_destroy(allocator);
        }
    }
}

static int teardown_task(void *ctx, h2_io *io) 
{
    h2_mplx *m = (h2_mplx *)ctx;
    if (io->task) {
        h2_task_teardown(io->task);
    }
    return 1;
}

void h2_mplx_cleanup(h2_mplx *m)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io_set_iter(m->task_finished_ios, teardown_task, m);
        h2_io_set_remove_all(m->task_finished_ios);
        
        apr_thread_mutex_unlock(m->lock);
    }
}

apr_pool_t *h2_mplx_get_pool(h2_mplx *m)
{
    assert(m);
    return m->pool;
}

conn_rec *h2_mplx_get_conn(h2_mplx *m)
{
    return m->c;
}

long h2_mplx_get_id(h2_mplx *m)
{
    assert(m);
    return m->id;
}

apr_size_t h2_mplx_get_out_max_mem(h2_mplx *m)
{
    return m->out_stream_max_size;
}

void h2_mplx_abort(h2_mplx *m)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        m->aborted = 1;
        h2_io_set_destroy_all(m->stream_ios);
        apr_thread_mutex_unlock(m->lock);
    }
}


static void task_finished(void *ctx, h2_task *task) 
{
    h2_mplx *m = (h2_mplx*)ctx;
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        m->aborted = 1;
        h2_io *io = h2_io_set_get(m->stream_ios, task->stream_id);
        if (io) {
            io->task = task;
            h2_io_set_add(m->task_finished_ios, io);
        }
        apr_thread_mutex_unlock(m->lock);
    }
}

apr_status_t h2_mplx_register_task(h2_mplx *m, h2_task *task)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, task->stream_id);
        if (io) {
            io->task = task;
            h2_task_on_finished(task, task_finished, m);
        }
        status = io? APR_SUCCESS : APR_EINVAL;
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_open_io(h2_mplx *m, int stream_id)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (!io) {
            io = h2_io_create(stream_id, m->pool, m->bucket_alloc);
            h2_io_set_add(m->stream_ios, io);
        }
        status = io? APR_SUCCESS : APR_ENOMEM;
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

void h2_mplx_close_io(h2_mplx *m, int stream_id)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            h2_io_set_remove(m->stream_ios, io);
            h2_io_destroy(io);
        }
        apr_thread_mutex_unlock(m->lock);
    }
}

apr_status_t h2_mplx_in_read(h2_mplx *m, apr_read_type_e block,
                             int stream_id, struct h2_bucket **pbucket,
                             struct apr_thread_cond_t *iowait)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            status = h2_io_in_read(io, pbucket);
            while (status == APR_EAGAIN 
                   && !is_aborted(m, &status)
                   && block == APR_BLOCK_READ) {
                io->input_arrived = iowait;
                apr_thread_cond_wait(io->input_arrived, m->lock);
                io->input_arrived = NULL;
                
                status = h2_io_in_read(io, pbucket);
            }
        }
        else {
            status = APR_EOF;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_in_write(h2_mplx *m,
                              int stream_id, struct h2_bucket *bucket)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            status = h2_io_in_write(io, bucket);
            if (io->input_arrived) {
                apr_thread_cond_signal(io->input_arrived);
            }
            apr_thread_mutex_unlock(m->lock);
        }
        else {
            status = APR_EOF;
        }
    }
    return status;
}

apr_status_t h2_mplx_in_close(h2_mplx *m, int stream_id)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            status = h2_io_in_close(io);
            if (io->input_arrived) {
                apr_thread_cond_signal(io->input_arrived);
            }
        }
        else {
            status = APR_ECONNABORTED;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

typedef struct {
    h2_mplx_consumed_cb *cb;
    void *cb_ctx;
    int streams_updated;
} update_ctx;

static int update_window(void *ctx, h2_io *io)
{
    if (io->input_consumed) {
        update_ctx *uctx = (update_ctx*)ctx;
        uctx->cb(uctx->cb_ctx, io->id, io->input_consumed);
        io->input_consumed = 0;
        ++uctx->streams_updated;
    }
    return 1;
}

apr_status_t h2_mplx_in_update_windows(h2_mplx *m, 
                                       h2_mplx_consumed_cb *cb, void *cb_ctx)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        update_ctx ctx = { cb, cb_ctx, 0 };
        h2_io_set_iter(m->stream_ios, update_window, &ctx);
        status = ctx.streams_updated? APR_SUCCESS : APR_EAGAIN;
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_read(h2_mplx *m, int stream_id, 
                              apr_bucket_brigade *bb, apr_size_t maxlen)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            status = h2_io_out_read(io, bb, maxlen);
            if (status == APR_SUCCESS && io->output_drained) {
                apr_thread_cond_signal(io->output_drained);
            }
        }
        else {
            status = APR_EAGAIN;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_reset(h2_mplx *m, int stream_id, apr_status_t ss)
{
    assert(m);
    return h2_mplx_out_open(m, stream_id, 
                            h2_response_create(stream_id, ss, NULL, NULL,
                                               m->pool),
                            NULL, NULL, NULL);
}

h2_response *h2_mplx_pop_response(h2_mplx *m, apr_bucket_brigade *bb)
{
    assert(m);
    h2_response *response = NULL;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get_highest_prio(m->ready_ios);
        if (io && io->response) {
            response = h2_io_extract_response(io);
            h2_io_set_remove(m->ready_ios, io);
            if (bb) {
                h2_io_out_read(io, bb, 0);
            }
            
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, m->c,
                          "h2_mplx(%ld): popped response(%d)",
                          m->id, response->stream_id);
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return response;
}

static apr_status_t out_write(h2_mplx *m, h2_io *io, 
                              ap_filter_t* f, apr_bucket_brigade *bb,
                              struct apr_thread_cond_t *iowait)
{
    apr_status_t status = APR_SUCCESS;
    /* We check the memory footprint queued for this stream_id
     * and block if it exceeds our configured limit.
     * We will not split buckets to enforce the limit to the last
     * byte. After all, the bucket is already in memory.
     */
    while (!APR_BRIGADE_EMPTY(bb) 
           && (status == APR_SUCCESS)
           && !is_aborted(m, &status)) {
        
        status = h2_io_out_write(io, bb, m->out_stream_max_size);
        
        /* Wait for data to drain until there is room again */
        while (!APR_BRIGADE_EMPTY(bb) 
               && status == APR_SUCCESS
               && (m->out_stream_max_size <= h2_io_out_length(io))
               && !is_aborted(m, &status)) {
            io->output_drained = iowait;
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, f->c,
                          "h2_mplx(%ld-%d): waiting for out drain", 
                          m->id, io->id);
            apr_thread_cond_wait(io->output_drained, m->lock);
            io->output_drained = NULL;
        }
    }
    return status;
}

apr_status_t h2_mplx_out_open(h2_mplx *m, int stream_id, h2_response *response,
                              ap_filter_t* f, apr_bucket_brigade *bb,
                              struct apr_thread_cond_t *iowait)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            io->response = response;
            h2_io_set_add(m->ready_ios, io);
            if (f && bb && iowait) {
                ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, f->c,
                              "h2_mplx(%ld-%d): open response",
                              m->id, stream_id);
                status = out_write(m, io, f, bb, iowait);
            }
            have_out_data_for(m, stream_id);
        }
        else {
            status = APR_ECONNABORTED;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}


apr_status_t h2_mplx_out_write(h2_mplx *m, int stream_id, 
                               ap_filter_t* f, apr_bucket_brigade *bb,
                               struct apr_thread_cond_t *iowait)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            status = out_write(m, io, f, bb, iowait);
            have_out_data_for(m, stream_id);
        }
        else {
            status = APR_ECONNABORTED;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_close(h2_mplx *m, int stream_id)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            status = h2_io_out_close(io);
            have_out_data_for(m, stream_id);
        }
        else {
            status = APR_ECONNABORTED;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

int h2_mplx_in_has_eos_for(h2_mplx *m, int stream_id)
{
    assert(m);
    int has_eos = 0;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            has_eos = h2_io_in_has_eos_for(io);
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return has_eos;
}

int h2_mplx_out_has_data_for(h2_mplx *m, int stream_id)
{
    assert(m);
    int has_data = 0;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            has_data = h2_io_out_has_data(io);
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return has_data;
}

apr_status_t h2_mplx_out_trywait(h2_mplx *m, apr_interval_time_t timeout,
                                 apr_thread_cond_t *iowait)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        m->added_output = iowait;
        status = apr_thread_cond_timedwait(m->added_output, m->lock, timeout);
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, m->c,
                      "h2_mplx(%ld): trywait on data for %f ms)",
                      m->id, timeout/1000.0);
        m->added_output = NULL;
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

static void have_out_data_for(h2_mplx *m, int stream_id)
{
    assert(m);
    if (m->added_output) {
        apr_thread_cond_signal(m->added_output);
    }
}

