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
#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_conn.h"
#include "h2_mplx.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_task_input.h"
#include "h2_task_output.h"
#include "h2_task.h"
#include "h2_ctx.h"

struct h2_task {
    const char *id;
    long session_id;
    int stream_id;
    int aborted;
    apr_uint32_t has_started;
    apr_uint32_t has_finished;
    
    struct h2_mplx *mplx;
    conn_rec *master;
    struct h2_conn *conn;
    
    struct h2_task_input *input;    /* http/1.1 input data */
    h2_bucket *first_input;
    int input_eos;

    struct h2_task_output *output;  /* response body data */
};



static ap_filter_rec_t *h2_input_filter_handle;
static ap_filter_rec_t *h2_output_filter_handle;

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


void h2_task_register_hooks(void)
{
    h2_input_filter_handle =
    ap_register_input_filter("H2_TO_HTTP", h2_filter_stream_input,
                             NULL, AP_FTYPE_NETWORK);
    
    h2_output_filter_handle =
    ap_register_output_filter("HTTP_TO_H2", h2_filter_stream_output,
                              NULL, AP_FTYPE_NETWORK);
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
    ap_add_input_filter_handle(h2_input_filter_handle,
                               task, NULL, c);
    ap_add_output_filter_handle(h2_output_filter_handle,
                                task, NULL, c);
    
    /* prevent processing by anyone else, including httpd core */
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_stream(%s): task_pre_conn, taking over", task->id);
    return DONE;
}


h2_task *h2_task_create(long session_id,
                        int stream_id,
                        conn_rec *master,
                        apr_pool_t *stream_pool,
                        h2_bucket *first_input,
                        int input_eos,
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
    task->session_id = session_id;
    task->mplx = mplx;
    task->master = master;
    task->first_input = first_input;
    task->input_eos = input_eos;
    
    h2_task_prep_conn(task);

    ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, stream_pool,
                  "h2_task(%s): created", task->id);
    return task;
}

apr_status_t h2_task_prep_conn(h2_task *task)
{
    /* We need a separate pool for the task execution as this happens
     * in another thread and pools are not multi-thread safe. 
     * Since the task lives not longer than the stream, we'd tried
     * making this new pool a sub pool of the stream one, but that
     * only led to crashes. With a root pool, this does not happen.
     */
    task->conn = h2_conn_create(task->id, task->master, 
                                h2_mplx_get_pool(task->mplx));
    if (!task->conn) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_ENOMEM, task->master,
                      "h2_task(%s): create task connection", task->id);
        h2_mplx_out_reset(task->mplx, task->stream_id, APR_ENOMEM);
        return APR_ENOMEM;
    }

    task->input = h2_task_input_create(task->conn->pool,
                                       task->id, task->stream_id,
                                       task->first_input, task->input_eos, 
                                       task->mplx);
    task->output = h2_task_output_create(task->conn->pool,
                                         task, task->stream_id,
                                         task->mplx);
    return APR_SUCCESS;
}


apr_status_t h2_task_destroy(h2_task *task)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->master,
                  "h2_task(%s): destroy started", task->id);
    if (task->input) {
        h2_task_input_destroy(task->input);
        task->input = NULL;
    }
    if (task->output) {
        h2_task_output_destroy(task->output);
        task->output = NULL;
    }
    if (task->mplx) {
        task->mplx = NULL;
    }
    if (task->conn) {
        h2_conn_destroy(task->conn);
        task->conn = NULL;
    }
    return APR_SUCCESS;
}

apr_status_t h2_task_do(h2_task *task, apr_thread_t *thd)
{
    assert(task);
    
    apr_status_t status = h2_conn_prep(task->conn, thd);
    if (status == APR_SUCCESS) {
        
        h2_ctx_create_for(task->conn->c, task);
        status = h2_conn_process(task->conn);
    }
    
    if (!h2_task_output_has_started(task->output)) {
        h2_mplx_out_reset(task->mplx, task->stream_id, status);
    }
    
    return status;
}

void h2_task_abort(h2_task *task)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->conn->c,
                  "h2_task(%s): aborting task", task->id);
    task->aborted =  1;
    if (task->input) {
        h2_task_input_destroy(task->input);
        task->input = NULL;
    }
    if (task->output) {
        h2_task_output_destroy(task->output);
        task->output = NULL;
    }
}

int h2_task_is_aborted(h2_task *task)
{
    assert(task);
    return task->aborted;
}

long h2_task_get_session_id(h2_task *task)
{
    assert(task);
    return task->session_id;
}

int h2_task_get_stream_id(h2_task *task)
{
    assert(task);
    return task->stream_id;
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




