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
#include "h2_io.h"
#include "h2_io_set.h"
#include "h2_response.h"
#include "h2_mplx.h"

struct h2_mplx {
    long id;
    conn_rec *c;
    apr_pool_t *pool;
    
    h2_io_set *stream_ios;
    h2_io_set *ready_ios;
    
    apr_thread_mutex_t *lock;
    apr_thread_cond_t *added_input;
    apr_thread_cond_t *added_output;
    apr_thread_cond_t *removed_output;
    
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

static void have_in_data_for(h2_mplx *m, int stream_id);
static void have_out_data_for(h2_mplx *m, int stream_id);
static void consumed_out_data_for(h2_mplx *m, int stream_id);

h2_mplx *h2_mplx_create(conn_rec *c, apr_pool_t *pool)
{
    apr_status_t status = APR_SUCCESS;
    h2_config *conf = h2_config_get(c);
    assert(conf);
    h2_mplx *m = apr_pcalloc(pool, sizeof(h2_mplx));
    if (m) {
        m->id = c->id;
        m->c = c;
        m->pool = pool;
        
        m->stream_ios = h2_io_set_create(m->pool);
        m->ready_ios = h2_io_set_create(m->pool);
        m->out_stream_max_size =
            h2_config_geti(conf, H2_CONF_STREAM_MAX_MEM_SIZE);
        
        status = apr_thread_mutex_create(&m->lock, APR_THREAD_MUTEX_DEFAULT,
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
    assert(m);
    if (m->ready_ios) {
        h2_io_set_destroy(m->ready_ios);
        m->ready_ios = NULL;
    }
    if (m->stream_ios) {
        h2_io_set_destroy(m->stream_ios);
        m->stream_ios = NULL;
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

apr_status_t h2_mplx_start_io(h2_mplx *m, int stream_id)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (!io) {
            io = h2_io_create(stream_id, m->pool);
            h2_io_set_add(m->stream_ios, io);
        }
        status = io? APR_SUCCESS : APR_ENOMEM;
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

void h2_mplx_end_io(h2_mplx *m, int stream_id)
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
                             int stream_id, struct h2_bucket **pbucket)
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
                apr_thread_cond_wait(m->added_input, m->lock);
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
            have_in_data_for(m, stream_id);
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
            have_in_data_for(m, stream_id);
        }
        else {
            status = APR_ECONNABORTED;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_read(h2_mplx *m, int stream_id, 
                              struct h2_bucket **pbucket, int *peos)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            status = h2_io_out_read(io, pbucket, peos);
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, m->c,
                          "h2_mplx(%ld): read on stream_id-out(%d)",
                          m->id, stream_id);
            if (status == APR_SUCCESS) {
                consumed_out_data_for(m, stream_id);
            }
        }
        else {
            status = APR_EAGAIN;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_open(h2_mplx *m, int stream_id, h2_response *response)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            io->response = response;
            h2_io_set_add(m->ready_ios, io);
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, m->c,
                          "h2_mplx(%ld): response on stream(%d)",
                          m->id, stream_id);
            have_out_data_for(m, stream_id);
        }
        else {
            status = APR_ECONNABORTED;
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return status;
}

apr_status_t h2_mplx_out_reset(h2_mplx *m, int stream_id, apr_status_t ss)
{
    assert(m);
    return h2_mplx_out_open(m, stream_id, 
                            h2_response_create(stream_id, ss,
                                               NULL, NULL, NULL,
                                               m->pool));
}

h2_response *h2_mplx_pop_response(h2_mplx *m)
{
    assert(m);
    h2_response *response = NULL;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get_highest_prio(m->ready_ios);
        if (io && io->response) {
            response = io->response;
            io->response = NULL;
            h2_io_set_remove(m->ready_ios, io);
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, m->c,
                          "h2_mplx(%ld): popped response(%d)",
                          m->id, response->stream_id);
        }
        apr_thread_mutex_unlock(m->lock);
    }
    return response;
}

apr_status_t h2_mplx_out_write(h2_mplx *m, apr_read_type_e block,
                               int stream_id, struct h2_bucket *bucket)
{
    assert(m);
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        h2_io *io = h2_io_set_get(m->stream_ios, stream_id);
        if (io) {
            /* We check the memory footprint queued for this stream_id
             * and block if it exceeds our configured limit.
             * We will not split buckets to enforce the limit to the last
             * byte. After all, the bucket is already in memory.
             */
            while (!is_aborted(m, &status)
                   && (m->out_stream_max_size < h2_io_out_length(io))) {
                       apr_thread_cond_wait(m->removed_output, m->lock);
                   }
            
            if (!is_aborted(m, &status)) {
                status = h2_io_out_write(io, bucket);
                if (status == APR_SUCCESS) {
                    have_out_data_for(m, stream_id);
                }
            }
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

apr_status_t h2_mplx_out_trywait(h2_mplx *m, apr_interval_time_t timeout)
{
    assert(m);
    int has_data = 0;
    apr_status_t status = apr_thread_mutex_lock(m->lock);
    if (APR_SUCCESS == status) {
        status = apr_thread_cond_timedwait(m->added_output, m->lock, timeout);
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, m->c,
                      "h2_mplx(%ld): trywait on data for %f ms)",
                      m->id, timeout/1000.0);
        apr_thread_mutex_unlock(m->lock);
    }
    return has_data;
}

static void have_in_data_for(h2_mplx *m, int stream_id)
{
    assert(m);
    apr_thread_cond_broadcast(m->added_input);
}

static void have_out_data_for(h2_mplx *m, int stream_id)
{
    assert(m);
    apr_thread_cond_broadcast(m->added_output);
}

static void consumed_out_data_for(h2_mplx *m, int stream_id)
{
    assert(m);
    apr_thread_cond_broadcast(m->removed_output);
}

