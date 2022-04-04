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

#include <apr_atomic.h>
#include <apr_thread_cond.h>
#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_conn.h"
#include "h2_from_h1.h"
#include "h2_mplx.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_task_input.h"
#include "h2_task_output.h"
#include "h2_task.h"
#include "h2_ctx.h"
#include "h2_worker.h"


static apr_status_t h2_filter_stream_input(ap_filter_t* filter,
                                           apr_bucket_brigade* brigade,
                                           ap_input_mode_t mode,
                                           apr_read_type_e block,
                                           apr_off_t readbytes) {
    h2_task *task = (h2_task *)filter->ctx;
    assert(task);
    if (!task->input) {
        return APR_ECONNABORTED;
    }
    return h2_task_input_read(task->input, filter, brigade,
                              mode, block, readbytes);
}

static apr_status_t h2_filter_stream_output(ap_filter_t* filter,
                                            apr_bucket_brigade* brigade) {
    h2_task *task = (h2_task *)filter->ctx;
    assert(task);
    if (!task->output) {
        return APR_ECONNABORTED;
    }
    return h2_task_output_write(task->output, filter, brigade);
}

static apr_status_t h2_filter_read_response(ap_filter_t* f,
                                            apr_bucket_brigade* bb) {
    h2_task *task = (h2_task *)f->ctx;
    assert(task);
    if (!task->output || !task->output->from_h1) {
        return APR_ECONNABORTED;
    }
    return h2_from_h1_read_response(task->output->from_h1, f, bb);
}

void h2_task_register_hooks(void)
{
    ap_register_input_filter("H2_TO_H1", h2_filter_stream_input,
                             NULL, AP_FTYPE_NETWORK);
    ap_register_output_filter("H1_TO_H2", h2_filter_stream_output,
                              NULL, AP_FTYPE_NETWORK);
    ap_register_output_filter("H1_TO_H2_RESP", h2_filter_read_response,
                              NULL, AP_FTYPE_PROTOCOL);
}

int h2_task_pre_conn(h2_task *task, conn_rec *c)
{
    assert(task);
    /* Add our own, network level in- and output filters.
     * These will take input from the h2_session->request_data
     * bucket queue and place the output into the
     * h2_session->response_data bucket queue.
     */
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_stream(%s): task_pre_conn, installing filters",
                  task->id);
    
    ap_add_input_filter("H2_TO_H1", task, NULL, c);
    ap_add_output_filter("H1_TO_H2", task, NULL, c);
    
    /* prevent processing by anyone else, including httpd core */
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_stream(%s): task_pre_conn, taking over", task->id);
    return DONE;
}


apr_status_t h2_task_setup(h2_task *task, conn_rec *master, 
                           apr_pool_t *parent, 
                           apr_bucket_alloc_t *bucket_alloc)
{
    /* We need a separate pool for the task execution as this happens
     * in another thread and pools are not multi-thread safe. 
     * Since the task lives not longer than the stream, we'd tried
     * making this new pool a sub pool of the stream one, but that
     * only led to crashes. With a root pool, this does not happen.
     */
    task->conn = h2_conn_create(task->id, master, parent, bucket_alloc);
    if (!task->conn) {
        return APR_ENOMEM;
    }
    
    task->input = h2_task_input_create(task->conn->pool,
                                       task, task->stream_id, task->mplx);
    task->output = h2_task_output_create(task->conn->pool,
                                         task, task->stream_id, task->mplx);
    return APR_SUCCESS;
}

h2_task *h2_task_create(long session_id,
                        int stream_id,
                        conn_rec *master,
                        apr_pool_t *stream_pool,
                        h2_mplx *mplx)
{
    h2_task *task = apr_pcalloc(stream_pool, sizeof(h2_task));
    if (task == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, APR_ENOMEM, stream_pool,
                      "h2_task(%ld-%d): create stream task", 
                      session_id, stream_id);
        h2_mplx_out_reset(mplx, stream_id, APR_ENOMEM);
        return NULL;
    }
    
    task->id = apr_psprintf(stream_pool, "%ld-%d", session_id, stream_id);
    task->stream_id = stream_id;
    task->mplx = mplx;
    
    h2_task_setup(task, h2_mplx_get_conn(task->mplx), 
                  h2_mplx_get_pool(task->mplx), 
                  apr_bucket_alloc_create(stream_pool));
    
    ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, stream_pool,
                  "h2_task(%s): created", task->id);
    return task;
}


apr_status_t h2_task_teardown(h2_task *task)
{
    assert(task);
    if (task->input) {
        h2_task_input_destroy(task->input);
        task->input = NULL;
    }
    if (task->output) {
        h2_task_output_destroy(task->output);
        task->output = NULL;
    }
    return APR_SUCCESS;
}

apr_status_t h2_task_destroy(h2_task *task)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, h2_mplx_get_conn(task->mplx),
                  "h2_task(%s): destroy started", task->id);
    if (task->mplx) {
        task->mplx = NULL;
    }
    apr_status_t status = h2_task_teardown(task);
    if (task->conn) {
        h2_conn_destroy(task->conn);
        task->conn = NULL;
    }
    return status;
}

void h2_task_on_finished(h2_task *task, task_callback *cb, void *cb_ctx)
{
    task->on_finished = cb;
    task->ctx_finished = cb_ctx;
}

apr_status_t h2_task_do(h2_task *task, h2_worker *worker)
{
    assert(task);
    
    apr_status_t status = h2_conn_prep(task->conn, 
                                       h2_worker_get_thread(worker));
    if (status == APR_SUCCESS) {
        /* save in connection that this one is for us, prevents
         * other hooks from messing with it. */
        h2_ctx_create_for(task->conn->c, task);
        /* borrow the condition from the worker during our processing. we
         * will use it for io blocking and signalling. */
        task->io = h2_worker_get_cond(worker);
        assert(task->io);
        
        status = h2_conn_process(task->conn);
        
        task->io = NULL;
    }
    
    if (!h2_task_output_has_started(task->output)) {
        h2_mplx_out_reset(task->mplx, task->stream_id, status);
    }
    
    if (task->on_finished) {
        task->on_finished(task->ctx_finished, task);
    }
    return status;
}

void h2_task_abort(h2_task *task)
{
    assert(task);
    task->aborted =  1;
}

int h2_task_is_aborted(h2_task *task)
{
    assert(task);
    return task->aborted;
}

void h2_task_interrupt(h2_task *task)
{
    apr_thread_cond_t *cond = task->io;
    if (cond) {
        /* task is waiting on io */
        apr_thread_cond_broadcast(cond);
    }
}

const char *h2_task_get_id(h2_task *task)
{
    return task->id;
}

int h2_task_has_started(h2_task *task)
{
    assert(task);
    return apr_atomic_read32(&task->has_started);
}

void h2_task_set_started(h2_task *task, int started)
{
    assert(task);
    apr_atomic_set32(&task->has_started, started);
}

int h2_task_has_finished(h2_task *task)
{
    assert(task);
    return apr_atomic_read32(&task->has_finished);
}

void h2_task_set_finished(h2_task *task, int finished)
{
    assert(task);
    apr_atomic_set32(&task->has_finished, finished);
}

apr_thread_cond_t *h2_task_get_io_cond(h2_task *task)
{
    return task->io;
}




