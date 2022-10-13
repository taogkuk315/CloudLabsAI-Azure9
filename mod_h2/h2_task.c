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
#include <http_protocol.h>
#include <http_request.h>
#include <http_log.h>
#include <http_vhost.h>
#include <util_filter.h>
#include <ap_mpm.h>
#include <mod_core.h>
#include <scoreboard.h>

#include "h2_private.h"
#include "h2_conn.h"
#include "h2_from_h1.h"
#include "h2_h2.h"
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
    ap_register_output_filter("H2_RESPONSE", h2_response_output_filter,
                              NULL, AP_FTYPE_PROTOCOL);
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
        h2_mplx_out_close(mplx, stream_id);
        return NULL;
    }
    
    APR_RING_ELEM_INIT(task, link);

    task->id = apr_psprintf(stream_pool, "%ld-%d", session_id, stream_id);
    task->stream_id = stream_id;
    task->master = master;
    task->stream_pool = stream_pool;
    task->mplx = mplx;
    
    /* We would like to have this happening when our task is about
     * to be processed by the worker. But something corrupts our
     * stream pool if we comment this out.
     * TODO.
     */
    task->conn = h2_conn_create(task->id, task->master, stream_pool);
    if (task->conn == NULL) {
        return NULL;
    }

    ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, stream_pool,
                  "h2_task(%s): created", task->id);
    return task;
}

void h2_task_set_request(h2_task *task, 
                         const char *method, const char *path, 
                         const char *authority, apr_table_t *headers, int eos)
{
    task->method = method;
    task->path = path;
    task->authority = authority;
    task->headers = headers;
    task->input_eos = eos;
}

apr_status_t h2_task_destroy(h2_task *task)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, h2_mplx_get_conn(task->mplx),
                  "h2_task(%s): destroy started", task->id);
    if (task->mplx) {
        task->mplx = NULL;
    }
    if (task->conn) {
        h2_conn_destroy(task->conn);
        task->conn = NULL;
    }
    return APR_SUCCESS;
}

void h2_task_on_finished(h2_task *task, task_callback *cb, void *cb_ctx)
{
    task->on_finished = cb;
    task->ctx_finished = cb_ctx;
}

apr_status_t h2_task_do(h2_task *task, h2_worker *worker)
{
    apr_status_t status = APR_SUCCESS;
    task->serialize_request = 0;
    
    assert(task);
    status = h2_conn_prep(task->conn, worker);
    
    /* save in connection that this one is for us, prevents
     * other hooks from messing with it. */
    h2_ctx *ctx = h2_ctx_create_for(task->conn->c, task);

    if (status == APR_SUCCESS) {
        if (task->serialize_request) {
            task->input = h2_task_input_create(task->conn->pool,
                                               task, task->stream_id, 
                                               task->conn->bucket_alloc, 
                                               task->method, task->path,
                                               task->authority, task->headers,
                                               task->input_eos, task->mplx);
        }
        else {
            task->input = h2_task_input_create2(task->conn->pool,
                                                task, task->stream_id, 
                                                task->conn->bucket_alloc, 
                                                task->input_eos, task->mplx);
        }
        task->output = h2_task_output_create(task->conn->pool, task, 
                                             task->stream_id, 
                                             task->conn->bucket_alloc, 
                                             task->mplx);
        
        status = h2_conn_process(task->conn);
    }
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, task->conn->c,
                  "h2_task(%s):processing done", task->id);
    
    if (task->output) {
        h2_task_output_close(task->output);
    }
    
    if (task->on_finished) {
        task->on_finished(task->ctx_finished, task);
    }

    if (task->input) {
        h2_task_input_destroy(task->input);
        task->input = NULL;
    }
    
    if (task->output) {
        h2_task_output_destroy(task->output);
        task->output = NULL;
    }
    
    if (task->conn) {
        h2_conn_post(task->conn, worker);
        task->conn = NULL;
    }
    
    return status;
}

void h2_task_abort(h2_task *task)
{
    assert(task);
    task->aborted =  1;
    h2_task_interrupt(task);
}

int h2_task_is_aborted(h2_task *task)
{
    assert(task);
    return task->aborted;
}

void h2_task_interrupt(h2_task *task)
{
    if (task->io) {
        /* wake everyone waiting on this */
        apr_thread_cond_broadcast(task->io);
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

void h2_task_set_started(h2_task *task, apr_thread_cond_t *cond)
{
    assert(task);
    task->io = cond;
    apr_atomic_set32(&task->has_started, 1);
}

int h2_task_has_finished(h2_task *task)
{
    assert(task);
    return apr_atomic_read32(&task->has_finished);
}

void h2_task_set_finished(h2_task *task)
{
    apr_thread_cond_t *io = task->io;
    assert(task);
    
    task->io = NULL;
    apr_atomic_set32(&task->has_finished, 1);
    if (io) {
        apr_thread_cond_signal(io);
    }
}

apr_thread_cond_t *h2_task_get_io_cond(h2_task *task)
{
    return task->io;
}

request_rec *h2_task_create_request(h2_task *task)
{
    conn_rec *conn = task->conn->c;
    request_rec *r;
    apr_pool_t *p;
    const char *expect;
    int access_status = HTTP_OK;
    apr_socket_t *csd;
    apr_interval_time_t cur_timeout;
    
    
    apr_pool_create(&p, conn->pool);
    apr_pool_tag(p, "request");
    r = apr_pcalloc(p, sizeof(request_rec));
    AP_READ_REQUEST_ENTRY((intptr_t)r, (uintptr_t)conn);
    r->pool            = p;
    r->connection      = conn;
    r->server          = conn->base_server;
    
    r->user            = NULL;
    r->ap_auth_type    = NULL;
    
    r->allowed_methods = ap_make_method_list(p, 2);
    
    r->headers_in = apr_table_copy(r->pool, task->headers);
    r->trailers_in     = apr_table_make(r->pool, 5);
    r->subprocess_env  = apr_table_make(r->pool, 25);
    r->headers_out     = apr_table_make(r->pool, 12);
    r->err_headers_out = apr_table_make(r->pool, 5);
    r->trailers_out    = apr_table_make(r->pool, 5);
    r->notes           = apr_table_make(r->pool, 5);
    
    r->request_config  = ap_create_request_config(r->pool);
    /* Must be set before we run create request hook */
    
    r->proto_output_filters = conn->output_filters;
    r->output_filters  = r->proto_output_filters;
    r->proto_input_filters = conn->input_filters;
    r->input_filters   = r->proto_input_filters;
    ap_run_create_request(r);
    r->per_dir_config  = r->server->lookup_defaults;
    
    r->sent_bodyct     = 0;                      /* bytect isn't for body */
    
    r->read_length     = 0;
    r->read_body       = REQUEST_NO_BODY;
    
    r->status          = HTTP_OK;  /* Until further notice */
    r->header_only     = 0;
    r->the_request     = NULL;
    
    /* Begin by presuming any module can make its own path_info assumptions,
     * until some module interjects and changes the value.
     */
    r->used_path_info = AP_REQ_DEFAULT_PATH_INFO;
    
    r->useragent_addr = conn->client_addr;
    r->useragent_ip = conn->client_ip;
    
    ap_run_pre_read_request(r, conn);
    
    /* Time to populate r with the data we have. */
    r->request_time = apr_time_now();
    r->the_request = apr_psprintf(r->pool, "%s %s HTTP/1.1", 
                                  task->method, task->path);
    r->method = task->method;
    /* Provide quick information about the request method as soon as known */
    r->method_number = ap_method_number_of(r->method);
    if (r->method_number == M_GET && r->method[0] == 'H') {
        r->header_only = 1;
    }

    ap_parse_uri(r, task->path);
    r->protocol = "HTTP/1.1";
    r->proto_num = HTTP_VERSION(1, 1);
    
    r->hostname = task->authority;
    
    /* update what we think the virtual host is based on the headers we've
     * now read. may update status.
     */
    ap_update_vhost_from_headers(r);
    
    /* we may have switched to another server */
    r->per_dir_config = r->server->lookup_defaults;
    
    /*
     * Add the HTTP_IN filter here to ensure that ap_discard_request_body
     * called by ap_die and by ap_send_error_response works correctly on
     * status codes that do not cause the connection to be dropped and
     * in situations where the connection should be kept alive.
     */
    ap_add_input_filter_handle(ap_http_input_filter_handle,
                               NULL, r, r->connection);
    
    if (access_status != HTTP_OK
        || (access_status = ap_run_post_read_request(r))) {
        ap_die(access_status, r);
        ap_update_child_status(conn->sbh, SERVER_BUSY_LOG, r);
        ap_run_log_transaction(r);
        r = NULL;
        goto traceout;
    }
    
    AP_READ_REQUEST_SUCCESS((uintptr_t)r, (char *)r->method, 
                            (char *)r->uri, (char *)r->server->defn_name, 
                            r->status);
    return r;
traceout:
    AP_READ_REQUEST_FAILURE((uintptr_t)r);
    return r;
}


apr_status_t h2_task_process_request(h2_task *task)
{
    conn_rec *c = task->conn->c;
    request_rec *r;
    conn_state_t *cs = c->cs;
    apr_socket_t *csd = NULL;
    int mpm_state = 0;
    apr_bucket_brigade *bb;
    apr_bucket *b;

    r = h2_task_create_request(task);
    if (r && (r->status == HTTP_OK)) {
        if (cs)
            cs->state = CONN_STATE_HANDLER;
        ap_process_request(r);
        /* After the call to ap_process_request, the
         * request pool will have been deleted.  We set
         * r=NULL here to ensure that any dereference
         * of r that might be added later in this function
         * will result in a segfault immediately instead
         * of nondeterministic failures later.
         */
        r = NULL;
    }
    
    return APR_SUCCESS;
}




