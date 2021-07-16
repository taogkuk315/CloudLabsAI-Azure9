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

#include <assert.h>
#include <stddef.h>

#include <apr_atomic.h>
#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>
#include <http_connection.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_mplx.h"
#include "h2_session.h"
#include "h2_response.h"
#include "h2_resp_head.h"
#include "h2_stream.h"
#include "h2_task_input.h"
#include "h2_task_output.h"
#include "h2_task.h"
#include "h2_ctx.h"

struct h2_task {
    long session_id;
    int stream_id;
    h2_task_state_t state;
    int aborted;
    
    apr_pool_t *pool;
    conn_rec *c;
    struct h2_task_input *input;    /* http/1.1 input data */
    struct h2_task_output *output;  /* response body data */
    struct h2_response *response;     /* response meta data */
    
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
    /* Add our own, network level in- and output filters.
     * These will take input from the h2_session->request_data
     * bucket queue and place the output into the
     * h2_session->response_data bucket queue.
     */
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_stream(%ld-%d): task_pre_conn, installing filters",
                  task->session_id, task->stream_id);
    ap_add_input_filter_handle(h2_input_filter_handle,
                               task, NULL, c);
    ap_add_output_filter_handle(h2_output_filter_handle,
                                task, NULL, c);
    
    /* prevent processing by anyone else, including httpd core */
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_stream(%ld-%d): task_pre_conn, taking over",
                  task->session_id, task->stream_id);
    return DONE;
}


static apr_sockaddr_t *h2_sockaddr_dup(apr_sockaddr_t *in, apr_pool_t *pool)
{
    apr_sockaddr_t *out = apr_pcalloc(pool, sizeof(apr_sockaddr_t));
    memcpy(out, in, sizeof(apr_sockaddr_t));
    out->pool = pool;
    
    if (in->hostname != NULL) {
        out->hostname = apr_pstrdup(pool, in->hostname);
    }
    if (in->servname != NULL) {
        out->servname = apr_pstrdup(pool, in->servname);
    }
    if (in->ipaddr_ptr != NULL) {
        ptrdiff_t offset = (char *)in->ipaddr_ptr - (char *)in;
        out->ipaddr_ptr = (char *)out + offset;
    }
    if (in->next != NULL) {
        out->next = h2_sockaddr_dup(in->next, pool);
    }
    
    return out;
}

static apr_status_t h2_conn_create(conn_rec **pc, conn_rec *master,
                                   apr_pool_t *pool)
{
    /* Setup a apache connection record for this stream.
     * Most of the know how borrowed from mod_spdy::slave_connection.cc
     */
    conn_rec *c = apr_pcalloc(pool, sizeof(conn_rec));
    
    c->pool = pool;
    c->bucket_alloc = apr_bucket_alloc_create(pool);
    c->conn_config = ap_create_conn_config(pool);
    c->notes = apr_table_make(pool, 5);
    
    /* We work only with mpm_worker at the moment. We need
     * more magic incantations to satisfy mpm_event.
     * 
     */
    c->cs = apr_pcalloc(pool, sizeof(conn_state_t));
    c->cs->state = CONN_STATE_READ_REQUEST_LINE;
    
    
    c->base_server = master->base_server;
    c->local_addr = h2_sockaddr_dup(master->local_addr, pool);
    c->local_ip = apr_pstrdup(pool, master->local_ip);
    c->client_addr = h2_sockaddr_dup(master->client_addr, pool);
    c->client_ip = apr_pstrdup(pool, master->client_ip);
    
    /* The juicy bit here is to guess a new connection id, as it
     * needs to be unique in this httpd instance, but there is
     * no API to allocate one.
     */
    // FIXME
    c->id = (long)master->id^(long)c;
    *pc = c;
    
    ap_log_perror(APLOG_MARK, APLOG_TRACE3, 0, pool,
                  "h2_task: created con %ld from master %ld",
                  c->id, master->id);
    
    return APR_SUCCESS;
}

static void set_state(h2_task *task, h2_task_state_t state);
static void response_state_change(h2_response *resp,
                                  h2_response_state_t prevstate,
                                  void *cb_ctx);
static apr_status_t output_convert(h2_bucket *bucket,
                                   void *conv_ctx,
                                   const char *data, apr_size_t len,
                                   apr_size_t *pconsumed);

h2_task *h2_task_create(long session_id,
                        int stream_id,
                        conn_rec *master,
                        h2_bucket *input,
                        h2_mplx *mplx)
{
    apr_pool_t *pool = NULL;
    apr_status_t status = apr_pool_create_ex(&pool, NULL, NULL, NULL);
    if (status != APR_SUCCESS) {
        return NULL;
    }
    
    conn_rec *c = NULL;
    status = h2_conn_create(&c, master, pool);
    if (status != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, status, pool,
                      "h2_task(%ld-%d): unable to create stream task",
                      session_id, stream_id);
        return NULL;
    }
    
    h2_task *task = apr_pcalloc(pool, sizeof(h2_task));
    if (task == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, APR_ENOMEM, pool,
                      "h2_task(%ld-%d): unable to create stream task",
                      session_id, stream_id);
        return NULL;
    }
    
    task->stream_id = stream_id;
    task->session_id = session_id;
    task->pool = pool;
    task->c = c;
    task->state = H2_TASK_ST_IDLE;
    task->input = h2_task_input_create(task->c->pool,
                                       session_id, stream_id,
                                       input, mplx);
    task->output = h2_task_output_create(task->c->pool,
                                         session_id, stream_id, mplx);
    
    task->response = h2_response_create(stream_id, task->c->pool);
    h2_response_set_state_change_cb(task->response,
                                    response_state_change, task);
    h2_task_output_set_converter(task->output, output_convert, task);
    
    h2_ctx_create_for(task->c, task);
    
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c,
                  "h2_task(%ld-%d): created", task->session_id, task->stream_id);
    return task;
}

apr_status_t h2_task_destroy(h2_task *task)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c,
                  "h2_task(%ld-%d): destroy started",
                  task->session_id, task->stream_id);
    if (task->response) {
        h2_response_destroy(task->response);
        task->response = NULL;
    }
    if (task->input) {
        h2_task_input_destroy(task->input);
        task->input = NULL;
    }
    if (task->output) {
        h2_task_output_destroy(task->output);
        task->output = NULL;
    }
    if (task->pool) {
        apr_pool_t *pool = task->pool;
        task->pool = NULL;
        apr_pool_destroy(pool);
    }
    return APR_SUCCESS;
}

apr_status_t h2_task_do(h2_task *task)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c,
                  "h2_task(%ld-%d): do", task->session_id, task->stream_id);
    apr_status_t status;
    
    /* Furthermore, other code might want to see the socket for
     * this connection. Allocate one without further function...
     */
    apr_socket_t *socket = NULL;
    status = apr_socket_create(&socket,
                               APR_INET, SOCK_STREAM,
                               APR_PROTO_TCP, task->pool);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, task->c,
                      "h2_stream_process, unable to alloc socket");
        return status;
    }
    
    set_state(task, H2_TASK_ST_STARTED);
    
    /* Incantations from mod_spdy. Peek and poke until the core
     * and other modules like mod_reqtimeout are happy */
    ap_set_module_config(task->c->conn_config, &core_module, socket);
    
    ap_process_connection(task->c, socket);
    
    apr_socket_close(socket);
    
    set_state(task, H2_TASK_ST_DONE);
    
    return APR_SUCCESS;
}

void h2_task_abort(h2_task *task)
{
    assert(task);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c,
                  "h2_task(%ld-%d): aborting task",
                  task->session_id, task->stream_id);
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

long h2_task_get_session_id(h2_task *task)
{
    return task->session_id;
}

int h2_task_get_stream_id(h2_task *task)
{
    return task->stream_id;
}

static void set_state(h2_task *task, h2_task_state_t state)
{
    assert(task);
    if (task->state != state) {
        h2_task_state_t oldstate = task->state;
        task->state = state;
        ap_log_cerror(APLOG_MARK, APLOG_TRACE3, 0, task->c,
                      "h2_task(%ld-%d): state now %d, was %d",
                      task->session_id, task->stream_id,
                      task->state, oldstate);
        if (state == H2_TASK_ST_READY) {
            /* task needs to submit the head of the response */
            apr_status_t status =
            h2_task_output_open(task->output, task->response);
            if (status != APR_SUCCESS) {
                ap_log_cerror( APLOG_MARK, APLOG_ERR, status, task->c,
                              "task(%ld-%d): starting response",
                              task->session_id, task->stream_id);
            }
        }
    }
}


static void response_state_change(h2_response *resp,
                                  h2_response_state_t prevstate,
                                  void *cb_ctx)
{
    switch (h2_response_get_state(resp)) {
        case H2_RESP_ST_BODY:
        case H2_RESP_ST_DONE: {
            h2_task *task = (h2_task *)cb_ctx;
            assert(task);
            if (task->state < H2_TASK_ST_READY) {
                set_state(task, H2_TASK_ST_READY);
            }
            break;
        }
        default:
            /* nop */
            break;
    }
}

static apr_status_t output_convert(h2_bucket *bucket,
                                   void *conv_ctx,
                                   const char *data, apr_size_t len,
                                   apr_size_t *pconsumed)
{
    h2_task *task = (h2_task *)conv_ctx;
    assert(task);
    return h2_response_http_convert(bucket, task->response,
                                    data, len, pconsumed);
}

