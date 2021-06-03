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
#include "h2_bucket_queue.h"
#include "h2_session.h"
#include "h2_response.h"
#include "h2_resp_head.h"
#include "h2_stream.h"
#include "h2_task_input.h"
#include "h2_task_output.h"
#include "h2_task.h"
#include "h2_ctx.h"

static ap_filter_rec_t *h2_input_filter_handle;
static ap_filter_rec_t *h2_output_filter_handle;

static apr_status_t h2_filter_stream_input(ap_filter_t* filter,
                                           apr_bucket_brigade* brigade,
                                           ap_input_mode_t mode,
                                           apr_read_type_e block,
                                           apr_off_t readbytes) {
    h2_task *task = (h2_task *)filter->ctx;
    if (!task->input) {
        return APR_ECONNABORTED;
    }
    return h2_task_input_read(task->input, filter, brigade,
                              mode, block, readbytes);
}

static apr_status_t h2_filter_stream_output(ap_filter_t* filter,
                                            apr_bucket_brigade* brigade) {
    h2_task *task = (h2_task *)filter->ctx;
    if (!task->output) {
        return APR_ECONNABORTED;
    }
    return h2_task_output_write(task->output, filter, brigade);
}


void h2_task_hooks_init(void)
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
                  "h2_stream(%d-%d): task_pre_conn, installing filters",
                  task->session_id, task->stream_id);
    ap_add_input_filter_handle(h2_input_filter_handle,
                               task, NULL, c);
    ap_add_output_filter_handle(h2_output_filter_handle,
                                task, NULL, c);
    
    /* prevent processing by anyone else, including httpd core */
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_stream(%d-%d): task_pre_conn, taking over",
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

static apr_status_t h2_conn_create(conn_rec **pc, conn_rec *master)
{
    apr_pool_t *spool = NULL;
    apr_status_t status = apr_pool_create(&spool, NULL);
    if (status != APR_SUCCESS || spool == NULL) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, master,
                      "h2_task, unable to alloc new pool");
        return APR_ENOMEM;
    }
    
    /* Setup a apache connection record for this stream.
     * Most of the know how borrowed from mod_spdy::slave_connection.cc
     */
    conn_rec *c = apr_pcalloc(spool, sizeof(conn_rec));
    
    c->pool = spool;
    c->bucket_alloc = apr_bucket_alloc_create(spool);
    c->conn_config = ap_create_conn_config(spool);
    c->notes = apr_table_make(spool, 5);
    
    c->base_server = master->base_server;
    c->local_addr = h2_sockaddr_dup(master->local_addr, spool);
    c->local_ip = apr_pstrdup(spool, master->local_ip);
    c->client_addr = h2_sockaddr_dup(master->client_addr, spool);
    c->client_ip = apr_pstrdup(spool, master->client_ip);
    
    /* The juicy bit here is to guess a new connection id, as it
     * needs to be unique in this httpd instance, but there is
     * no API to allocate one.
     */
    // FIXME
    c->id = (int)master->id^(int)c;
    *pc = c;
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE3, 0, master,
                  "h2_task: created con %d from master %d",
                  (int)c->id, (int)master->id);
    
    return APR_SUCCESS;
}

static apr_status_t output_convert(h2_bucket *bucket,
                                   void *conv_ctx,
                                   const char *data, apr_size_t len,
                                   apr_size_t *pconsumed)
{
    h2_task *task = (h2_task *)conv_ctx;
    return h2_response_http_convert(bucket, task->response,
                                    data, len, pconsumed);
}

static void set_state(h2_task *task, h2_task_state_t state)
{
    if (task->state != state) {
        h2_task_state_t oldstate = task->state;
        task->state = state;
        ap_log_cerror(APLOG_MARK, APLOG_TRACE3, 0, task->c,
                      "h2_task(%d-%d): state now %d, was %d",
                      task->session_id, task->stream_id,
                      task->state, oldstate);
        if (state == H2_TASK_ST_READY && task->event_cb) {
            task->event_cb(task, H2_TASK_EV_READY, task->event_ctx);
        }
        else if (state == H2_TASK_ST_DONE && task->event_cb) {
            task->event_cb(task, H2_TASK_EV_DONE, task->event_ctx);
        }
    }
}

static void response_state_change(h2_response *resp,
                                  h2_response_state_t prevstate,
                                  void *cb_ctx)
{
    switch (resp->state) {
        case H2_RESP_ST_BODY:
        case H2_RESP_ST_DONE: {
            h2_task *task = (h2_task *)cb_ctx;
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

h2_task *h2_task_create(int session_id, int stream_id,
                        conn_rec *master,
                        h2_bucket_queue *input,
                        h2_bucket_queue *output)
{
    conn_rec *c = NULL;
    apr_status_t status = h2_conn_create(&c, master);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, master,
                      "h2_task(%d-%d): unable to create stream task",
                      session_id, stream_id);
        return NULL;
    }
    
    h2_task *task = apr_pcalloc(c->pool, sizeof(h2_task));
    if (task == NULL) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_ENOMEM, master,
                      "h2_task(%d-%d): unable to create stream task",
                      session_id, stream_id);
        return NULL;
    }
    
    task->stream_id = stream_id;
    task->session_id = session_id;
    task->c = c;
    task->state = H2_TASK_ST_IDLE;
    task->input = h2_task_input_create(task->c->pool, stream_id, input);
    task->output = h2_task_output_create(task->c->pool, stream_id, output);
    
    task->response = h2_response_create(stream_id, task->c);
    h2_response_set_state_change_cb(task->response,
                                    response_state_change, task);
    h2_task_output_set_converter(task->output, output_convert, task);
    
    h2_ctx_create_for(task->c, task);
    
    ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, task->c->pool,
                  "h2_task(%d-%d): created", task->session_id, task->stream_id);
    return task;
}

apr_status_t h2_task_destroy(h2_task *task)
{
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c,
                  "h2_task(%d-%d): destroy started",
                  task->session_id, task->stream_id);
    if (task->input) {
        h2_task_input_destroy(task->input);
        task->input = NULL;
    }
    if (task->output) {
        h2_task_output_destroy(task->output);
        task->output = NULL;
    }
    if (task->response) {
        h2_response_destroy(task->response);
        task->response = NULL;
    }
    if (0 && task->c->pool) {
        apr_pool_destroy(task->c->pool);
        task->c->pool = NULL;
    }
    return APR_SUCCESS;
}

apr_status_t h2_task_do(h2_task *task)
{
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->c,
                  "h2_task(%d-%d): do", task->session_id, task->stream_id);
    apr_status_t status;
    
    /* Furthermore, other code might want to see the socket for
     * this connection. Allocate one without further function...
     */
    apr_socket_t *socket = NULL;
    status = apr_socket_create(&socket,
                               APR_INET, SOCK_STREAM,
                               APR_PROTO_TCP, task->c->pool);
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
    
    if (task->aborted) {
        h2_task_destroy(task);
    }
    return APR_SUCCESS;
}

void h2_task_abort(h2_task *task)
{
    task->aborted =  1;
    task->event_cb = NULL;
    task->event_ctx = NULL;
    if (task->input) {
        h2_task_input_destroy(task->input);
        task->input = NULL;
    }
    if (task->output) {
        h2_task_output_destroy(task->output);
        task->output = NULL;
    }
}

void h2_task_set_event_cb(h2_task *task, h2_task_event_cb *cb, void *event_ctx)
{
    task->event_cb = cb;
    task->event_ctx = event_ctx;
}

h2_resp_head *h2_task_get_resp_head(h2_task *task)
{
    h2_resp_head *head = task->response->head;
    if (head) {
        task->response->head = NULL;
    }
    return head;
}

