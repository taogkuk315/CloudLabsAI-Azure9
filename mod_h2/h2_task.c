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
#include <http_log.h>
#include <http_connection.h>
#include <http_protocol.h>
#include <http_request.h>

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
    
    h2_mplx *mplx;
    conn_rec *master;
    
    int own_pool;
    apr_pool_t *pool;
    conn_rec *c;
    apr_socket_t *socket;
    
    struct h2_task_input *input;    /* http/1.1 input data */
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

typedef struct event_conn_state_t event_conn_state_t;
struct event_conn_state_t {
    /** APR_RING of expiration timeouts */
    APR_RING_ENTRY(event_conn_state_t) timeout_list;
    /** the expiration time of the next keepalive timeout */
    apr_time_t expiration_time;
    /** connection record this struct refers to */
    conn_rec *c;
    /** request record (if any) this struct refers to */
    request_rec *r;
    /** is the current conn_rec suspended?  (disassociated with
     * a particular MPM thread; for suspend_/resume_connection
     * hooks)
     */
    int suspended;
    /** memory pool to allocate from */
    apr_pool_t *p;
    /** bucket allocator */
    apr_bucket_alloc_t *bucket_alloc;
    /** poll file descriptor information */
    apr_pollfd_t pfd;
    /** public parts of the connection state */
    conn_state_t pub;
};
APR_RING_HEAD(timeout_head_t, event_conn_state_t);

static void fix_event_conn(h2_task *task, conn_rec *master) 
{
    event_conn_state_t *master_cs = ap_get_module_config(master->conn_config, 
                                                         h2_conn_mpm_module());
    event_conn_state_t *cs = apr_pcalloc(task->pool, sizeof(event_conn_state_t));
    cs->bucket_alloc = apr_bucket_alloc_create(task->pool);
    
    ap_set_module_config(task->c->conn_config, h2_conn_mpm_module(), cs);
    
    cs->c = task->c;
    cs->r = NULL;
    cs->p = master_cs->p;
    cs->pfd = master_cs->pfd;
    cs->pub = master_cs->pub;
    cs->pub.state = CONN_STATE_READ_REQUEST_LINE;
    
    task->c->cs = &(cs->pub);
}

static apr_status_t h2_conn_create(h2_task *task, conn_rec *master)
{
    assert(task);
    /* Setup a apache connection record for this stream.
     * General idea is borrowed from mod_spdy::slave_connection.cc,
     * partly replaced with some more modern calls to ap infrastructure.
     *
     * Here, we are tasting some sweet, internal knowledge, e.g. that
     * the core module is storing the connection socket as its config.
     * "ap_run_create_connection() needs a real socket as it tries to
     * detect local and client address information and fails if it is
     * unable to get it.
     * In case someone ever replaces these core hooks, this will probably
     * break miserably.
     */
    task->socket = ap_get_module_config(master->conn_config,
                                        &core_module);
    
    task->c = ap_run_create_connection(task->pool, master->base_server,
                                       task->socket,
                                       master->id^((long)task), master->sbh,
                                       apr_bucket_alloc_create(task->pool));
    if (task->c == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, task->pool,
                      "h2_task: creating conn failed");
        return APR_EGENERAL;
    }
    
    /* This works for mpm_worker so far. Other mpm modules have different needs,
     * unfortunately. The most interesting one being mpm_event...
     */
    switch (h2_conn_mpm_type()) {
        case H2_MPM_WORKER:
            /* all fine */
            break;
        case H2_MPM_EVENT: 
            fix_event_conn(task, master);
            break;
        default:
            /* fingers crossed */
            break;
    }
    
    ap_log_perror(APLOG_MARK, APLOG_TRACE3, 0, task->pool,
                  "h2_task: created con %ld from master %ld",
                  task->c->id, master->id);
    return APR_SUCCESS;
}

static apr_status_t setup_connection(h2_task *task, apr_pool_t *parent) {
    assert(task);
    apr_status_t status = APR_SUCCESS;
    if (task->own_pool) {
        status = apr_pool_create_ex(&task->pool, parent, NULL, NULL);
        apr_pool_tag(task->pool, task->id);
    }
    else {
        task->pool = parent;
    }
    
    if (status == APR_SUCCESS) {
        status = h2_conn_create(task, task->master);
        if (status == APR_SUCCESS) {
            h2_ctx_create_for(task->c, task);
        }
    }
    return status;
}

h2_task *h2_task_create(long session_id,
                        int stream_id,
                        conn_rec *master,
                        apr_pool_t *stream_pool,
                        h2_bucket *input,
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
    
    task->input = h2_task_input_create(stream_pool,
                                       task->id, task->stream_id,
                                       input, input_eos, task->mplx);
    task->output = h2_task_output_create(stream_pool,
                                         task, task->stream_id,
                                         task->mplx);
    /* We need a separate pool for the task execution as this happens
     * in another thread and pools are not multi-thread safe. 
     * Since the task lives not longer than the stream, we'd tried
     * making this new pool a sub pool of the stream one, but that
     * only led to crashes. With a root pool, this does not happen.
     */
    task->own_pool = 1;
    apr_status_t status = setup_connection(task, NULL);
    if (status != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, status, stream_pool,
                      "h2_task(%s): create task connection", task->id);
        h2_mplx_out_reset(mplx, task->stream_id, APR_ENOMEM);
        return NULL;
    }

    ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, stream_pool,
                  "h2_task(%s): created", task->id);
    return task;
}

apr_status_t h2_task_destroy(h2_task *task)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c,
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
    if (task->pool && task->own_pool) {
        apr_pool_destroy(task->pool);
    }
    return APR_SUCCESS;
}

apr_status_t h2_task_do(h2_task *task, apr_thread_t *thd)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c,
                  "h2_task(%s): do", task->id);
    
    task->c->current_thread = thd;
    
    /* Furthermore, other code might want to see the socket for
     * this connection. Allocate one without further function...
     */
    apr_status_t status = apr_socket_create(&task->socket,
                                            APR_INET, SOCK_STREAM,
                                            APR_PROTO_TCP, task->pool);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, task->c,
                      "h2_task/%s): alloc socket", task->id);
        h2_mplx_out_reset(task->mplx, task->stream_id, status);
        return status;
    }
    
    ap_set_module_config(task->c->conn_config, &core_module, task->socket);
    ap_process_connection(task->c, task->socket);

    if (!h2_task_output_has_started(task->output)) {
        h2_mplx_out_reset(task->mplx, task->stream_id, status);
    }
    
    apr_socket_close(task->socket);
    
    return APR_SUCCESS;
}

void h2_task_abort(h2_task *task)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c,
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




