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
#include "h2.h"
#include "h2_bucket_beam.h"
#include "h2_conn.h"
#include "h2_config.h"
#include "h2_ctx.h"
#include "h2_from_h1.h"
#include "h2_h2.h"
#include "h2_mplx.h"
#include "h2_request.h"
#include "h2_headers.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_task.h"
#include "h2_util.h"

static void H2_TASK_OUT_LOG(int lvl, conn_rec *c, apr_bucket_brigade *bb,
                            const char *tag)
{
    if (APLOG_C_IS_LEVEL(c, lvl)) {
        h2_conn_ctx_t *ctx = h2_conn_ctx_get(c);
        char buffer[4 * 1024];
        const char *line = "(null)";
        apr_size_t len, bmax = sizeof(buffer)/sizeof(buffer[0]);
        
        len = h2_util_bb_print(buffer, bmax, tag, "", bb);
        ap_log_cerror(APLOG_MARK, lvl, 0, c, "bb_dump(%s): %s", 
                      ctx->id, len? buffer : line);
    }
}

/*******************************************************************************
 * task output handling
 ******************************************************************************/

static apr_status_t open_output(h2_conn_ctx_t *conn_ctx, conn_rec *c)
{
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c, APLOGNO(03348)
                  "h2_task(%s): open output to %s %s %s",
                  conn_ctx->id, conn_ctx->request->method,
                  conn_ctx->request->authority,
                  conn_ctx->request->path);
    conn_ctx->task->output.opened = 1;
    return h2_mplx_t_out_open(conn_ctx->mplx, conn_ctx->stream_id, conn_ctx->task->output.beam);
}

static void output_consumed(void *ctx, h2_bucket_beam *beam, apr_off_t length)
{
    conn_rec *c = ctx;
    if (c && h2_task_logio_add_bytes_out) {
        h2_task_logio_add_bytes_out(c, length);
    }
}

static apr_status_t send_out(conn_rec *c, h2_conn_ctx_t *conn_ctx, apr_bucket_brigade* bb, int block)
{
    apr_off_t written, left;
    apr_status_t status;

    apr_brigade_length(bb, 0, &written);
    H2_TASK_OUT_LOG(APLOG_TRACE2, c, bb, "h2_task send_out");
    h2_beam_log(conn_ctx->task->output.beam, c, APLOG_TRACE2, "send_out(before)");

    status = h2_beam_send(conn_ctx->task->output.beam, bb,
                          block? APR_BLOCK_READ : APR_NONBLOCK_READ);
    h2_beam_log(conn_ctx->task->output.beam, c, APLOG_TRACE2, "send_out(after)");
    
    if (APR_STATUS_IS_EAGAIN(status)) {
        apr_brigade_length(bb, 0, &left);
        written -= left;
        status = APR_SUCCESS;
    }
    if (status == APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_task(%s): send_out done", conn_ctx->id);
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, c,
                      "h2_task(%s): send_out (%ld bytes)", 
                      conn_ctx->id, (long)written);
    }
    return status;
}

/* Bring the data from the brigade (which represents the result of the
 * request_rec out filter chain) into the h2_mplx for further sending
 * on the master connection. 
 */
static apr_status_t secondary_out(h2_conn_ctx_t *conn_ctx, ap_filter_t* f,
                                  apr_bucket_brigade* bb)
{
    apr_bucket *b;
    apr_status_t rv = APR_SUCCESS;
    int flush = 0, blocking;
    
send:
    /* we send block once we opened the output, so someone is there reading it */
    blocking = conn_ctx->task->output.opened;
    for (b = APR_BRIGADE_FIRST(bb);
         b != APR_BRIGADE_SENTINEL(bb);
         b = APR_BUCKET_NEXT(b)) {
        if (APR_BUCKET_IS_FLUSH(b) || APR_BUCKET_IS_EOS(b) || AP_BUCKET_IS_EOR(b)) {
            flush = 1;
            break;
        }
    }
    
    if (conn_ctx->task->output.bb && !APR_BRIGADE_EMPTY(conn_ctx->task->output.bb)) {
        /* still have data buffered from previous attempt.
         * setaside and append new data and try to pass the complete data */
        if (!APR_BRIGADE_EMPTY(bb)) {
            if (APR_SUCCESS != (rv = ap_save_brigade(f, &conn_ctx->task->output.bb, &bb, conn_ctx->pool))) {
                goto out;
            }
        }
        rv = send_out(f->c, conn_ctx, conn_ctx->task->output.bb, blocking);
    }
    else {
        /* no data buffered previously, pass brigade directly */
        rv = send_out(f->c, conn_ctx, bb, blocking);

        if (APR_SUCCESS == rv && !APR_BRIGADE_EMPTY(bb)) {
            /* output refused to buffer it all, time to open? */
            if (!conn_ctx->task->output.opened && APR_SUCCESS == (rv = open_output(conn_ctx, f->c))) {
                /* Make another attempt to send the data. With the output open,
                 * the call might be blocking and send all data, so we do not need
                 * to save the brigade */
                goto send;
            }
            else if (blocking && flush) {
                /* Need to keep on doing this. */
                goto send;
            }
            
            if (APR_SUCCESS == rv) {
                /* could not write all, buffer the rest */
                ap_log_cerror(APLOG_MARK, APLOG_DEBUG, rv, f->c, APLOGNO(03405)
                              "h2_secondary_out(%s): saving brigade", conn_ctx->id);
                ap_assert(NULL);
                rv = ap_save_brigade(f, &conn_ctx->task->output.bb, &bb, conn_ctx->pool);
                flush = 1;
            }
        }
    }
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, f->c,
                  "h2_secondary_out(%s): buffered=%d", conn_ctx->id, conn_ctx->task->output.buffered);
    if (APR_SUCCESS == rv && !conn_ctx->task->output.opened && (flush || !conn_ctx->task->output.buffered)) {
        /* got a flush or could not write all, time to tell someone to read */
        rv = open_output(conn_ctx, f->c);
    }
out:
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, rv, f->c,
                  "h2_secondary_out(%s): secondary_out leave", conn_ctx->id);
    return rv;
}

static apr_status_t output_finish(h2_conn_ctx_t *conn_ctx, conn_rec *c)
{
    if (!conn_ctx->task->output.opened) {
        return open_output(conn_ctx, c);
    }
    return APR_SUCCESS;
}

/*******************************************************************************
 * task secondary connection filters
 ******************************************************************************/

static apr_status_t h2_filter_secondary_in(ap_filter_t* f,
                                           apr_bucket_brigade* bb,
                                           ap_input_mode_t mode,
                                           apr_read_type_e block,
                                           apr_off_t readbytes)
{
    h2_conn_ctx_t *conn_ctx;
    apr_status_t status = APR_SUCCESS;
    apr_bucket *b, *next;
    apr_off_t bblen;
    const int trace1 = APLOGctrace1(f->c);
    apr_size_t rmax = ((readbytes <= APR_SIZE_MAX)? 
                       (apr_size_t)readbytes : APR_SIZE_MAX);
    
    conn_ctx = h2_conn_ctx_get(f->c);
    ap_assert(conn_ctx);

    if (trace1) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, f->c,
                      "h2_secondary_in(%s): read, mode=%d, block=%d, readbytes=%ld", 
                      conn_ctx->id, mode, block, (long)readbytes);
    }
    
    if (mode == AP_MODE_INIT) {
        return ap_get_brigade(f->c->input_filters, bb, mode, block, readbytes);
    }
    
    if (f->c->aborted) {
        return APR_ECONNABORTED;
    }
    
    if (!conn_ctx->task->input.bb) {
        return APR_EOF;
    }
    
    /* Cleanup brigades from those nasty 0 length non-meta buckets
     * that apr_brigade_split_line() sometimes produces. */
    for (b = APR_BRIGADE_FIRST(conn_ctx->task->input.bb);
         b != APR_BRIGADE_SENTINEL(conn_ctx->task->input.bb); b = next) {
        next = APR_BUCKET_NEXT(b);
        if (b->length == 0 && !APR_BUCKET_IS_METADATA(b)) {
            apr_bucket_delete(b);
        } 
    }
    
    while (APR_BRIGADE_EMPTY(conn_ctx->task->input.bb)) {
        /* Get more input data for our request. */
        if (trace1) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, f->c,
                          "h2_secondary_in(%s): get more data from mplx, block=%d, "
                          "readbytes=%ld", conn_ctx->id, block, (long)readbytes);
        }
        if (conn_ctx->task->input.beam) {
            status = h2_beam_receive(conn_ctx->task->input.beam, conn_ctx->task->input.bb, block,
                                     128*1024, NULL);
        }
        else {
            status = APR_EOF;
        }
        
        if (trace1) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, f->c,
                          "h2_secondary_in(%s): read returned", conn_ctx->id);
        }
        if (APR_STATUS_IS_EAGAIN(status) 
            && (mode == AP_MODE_GETLINE || block == APR_BLOCK_READ)) {
            /* chunked input handling does not seem to like it if we
             * return with APR_EAGAIN from a GETLINE read... 
             * upload 100k test on test-ser.example.org hangs */
            status = APR_SUCCESS;
        }
        else if (APR_STATUS_IS_EOF(status)) {
            break;
        }
        else if (status != APR_SUCCESS) {
            return status;
        }

        if (trace1) {
            h2_util_bb_log(f->c, conn_ctx->stream_id, APLOG_TRACE2,
                        "input.beam recv raw", conn_ctx->task->input.bb);
        }
        if (h2_task_logio_add_bytes_in) {
            apr_brigade_length(bb, 0, &bblen);
            h2_task_logio_add_bytes_in(f->c, bblen);
        }
    }
    
    /* Nothing there, no more data to get. Return. */
    if (status == APR_EOF && APR_BRIGADE_EMPTY(conn_ctx->task->input.bb)) {
        return status;
    }

    if (trace1) {
        h2_util_bb_log(f->c, conn_ctx->stream_id, APLOG_TRACE2,
                    "task_input.bb", conn_ctx->task->input.bb);
    }
           
    if (APR_BRIGADE_EMPTY(conn_ctx->task->input.bb)) {
        if (trace1) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, f->c,
                          "h2_secondary_in(%s): no data", conn_ctx->id);
        }
        return (block == APR_NONBLOCK_READ)? APR_EAGAIN : APR_EOF;
    }
    
    if (mode == AP_MODE_EXHAUSTIVE) {
        /* return all we have */
        APR_BRIGADE_CONCAT(bb, conn_ctx->task->input.bb);
    }
    else if (mode == AP_MODE_READBYTES) {
        status = h2_brigade_concat_length(bb, conn_ctx->task->input.bb, rmax);
    }
    else if (mode == AP_MODE_SPECULATIVE) {
        status = h2_brigade_copy_length(bb, conn_ctx->task->input.bb, rmax);
    }
    else if (mode == AP_MODE_GETLINE) {
        /* we are reading a single LF line, e.g. the HTTP headers. 
         * this has the nasty side effect to split the bucket, even
         * though it ends with CRLF and creates a 0 length bucket */
        status = apr_brigade_split_line(bb, conn_ctx->task->input.bb, block,
                                        HUGE_STRING_LEN);
        if (APLOGctrace1(f->c)) {
            char buffer[1024];
            apr_size_t len = sizeof(buffer)-1;
            apr_brigade_flatten(bb, buffer, &len);
            buffer[len] = 0;
            if (trace1) {
                ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, f->c,
                              "h2_secondary_in(%s): getline: %s",
                              conn_ctx->id, buffer);
            }
        }
    }
    else {
        /* Hmm, well. There is mode AP_MODE_EATCRLF, but we chose not
         * to support it. Seems to work. */
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_ENOTIMPL, f->c,
                      APLOGNO(03472) 
                      "h2_secondary_in(%s), unsupported READ mode %d", 
                      conn_ctx->id, mode);
        status = APR_ENOTIMPL;
    }
    
    if (trace1) {
        apr_brigade_length(bb, 0, &bblen);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, f->c,
                      "h2_secondary_in(%s): %ld data bytes", conn_ctx->id, (long)bblen);
    }
    return status;
}

static apr_status_t h2_filter_secondary_output(ap_filter_t* f,
                                               apr_bucket_brigade* brigade)
{
    h2_conn_ctx_t *conn_ctx = h2_conn_ctx_get(f->c);
    apr_status_t status;
    
    ap_assert(conn_ctx);
    status = secondary_out(conn_ctx, f, brigade);
    if (status != APR_SUCCESS) {
        if (conn_ctx->task->input.beam) {
            h2_beam_leave(conn_ctx->task->input.beam);
        }
        if (!conn_ctx->done) {
            h2_beam_abort(conn_ctx->task->output.beam);
        }
        f->c->aborted = 1;
    }
    return status;
}

static apr_status_t h2_filter_parse_h1(ap_filter_t* f, apr_bucket_brigade* bb)
{
    h2_conn_ctx_t *conn_ctx = h2_conn_ctx_get(f->c);
    apr_status_t status;
    
    ap_assert(conn_ctx);
    /* There are cases where we need to parse a serialized http/1.1 
     * response. One example is a 100-continue answer in serialized mode
     * or via a mod_proxy setup */
    while (bb && !f->c->aborted && !conn_ctx->task->output.sent_response) {
        status = h2_from_h1_parse_response(conn_ctx, f, bb);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, f->c,
                      "h2_task(%s): parsed response", conn_ctx->id);
        if (APR_BRIGADE_EMPTY(bb) || status != APR_SUCCESS) {
            return status;
        }
    }
    
    return ap_pass_brigade(f->next, bb);
}

/*******************************************************************************
 * task things
 ******************************************************************************/
 
int h2_task_is_running(conn_rec *c)
{
    h2_conn_ctx_t *conn_ctx = h2_conn_ctx_get(c);
    return conn_ctx && conn_ctx->started_at != 0 && !conn_ctx->done;
}

/*******************************************************************************
 * Register various hooks
 */
static const char *const mod_ssl[]        = { "mod_ssl.c", NULL};
static int h2_task_pre_conn(conn_rec* c, void *arg);
static int h2_task_process_conn(conn_rec* c);

APR_OPTIONAL_FN_TYPE(ap_logio_add_bytes_in) *h2_task_logio_add_bytes_in;
APR_OPTIONAL_FN_TYPE(ap_logio_add_bytes_out) *h2_task_logio_add_bytes_out;

void h2_task_register_hooks(void)
{
    /* This hook runs on new connections before mod_ssl has a say.
     * Its purpose is to prevent mod_ssl from touching our pseudo-connections
     * for streams.
     */
    ap_hook_pre_connection(h2_task_pre_conn,
                           NULL, mod_ssl, APR_HOOK_FIRST);
    /* When the connection processing actually starts, we might 
     * take over, if the connection is for a task.
     */
    ap_hook_process_connection(h2_task_process_conn, 
                               NULL, NULL, APR_HOOK_FIRST);

    ap_register_input_filter("H2_SECONDARY_IN", h2_filter_secondary_in,
                             NULL, AP_FTYPE_NETWORK);
    ap_register_output_filter("H2_SECONDARY_OUT", h2_filter_secondary_output,
                              NULL, AP_FTYPE_NETWORK);
    ap_register_output_filter("H2_PARSE_H1", h2_filter_parse_h1,
                              NULL, AP_FTYPE_NETWORK);

    ap_register_input_filter("H2_REQUEST", h2_filter_request_in,
                             NULL, AP_FTYPE_PROTOCOL);
    ap_register_output_filter("H2_RESPONSE", h2_filter_headers_out,
                              NULL, AP_FTYPE_PROTOCOL);
    ap_register_output_filter("H2_TRAILERS_OUT", h2_filter_trailers_out,
                              NULL, AP_FTYPE_PROTOCOL);
}

/* post config init */
apr_status_t h2_task_init(apr_pool_t *pool, server_rec *s)
{
    h2_task_logio_add_bytes_in = APR_RETRIEVE_OPTIONAL_FN(ap_logio_add_bytes_in);
    h2_task_logio_add_bytes_out = APR_RETRIEVE_OPTIONAL_FN(ap_logio_add_bytes_out);

    return APR_SUCCESS;
}

static int h2_task_pre_conn(conn_rec* c, void *arg)
{
    h2_conn_ctx_t *ctx;
    
    if (!c->master) {
        return OK;
    }
    
    ctx = h2_conn_ctx_get(c);
    (void)arg;
    if (ctx->task) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_secondary(%s), pre_connection, adding filters", c->log_id);
        ap_add_input_filter("H2_SECONDARY_IN", NULL, NULL, c);
        ap_add_output_filter("H2_PARSE_H1", NULL, NULL, c);
        ap_add_output_filter("H2_SECONDARY_OUT", NULL, NULL, c);
    }
    return OK;
}

h2_task *h2_task_create(conn_rec *secondary, h2_stream *stream)
{
    h2_conn_ctx_t *ctx;
    h2_task *task;

    ap_assert(secondary);
    ap_assert(stream);
    ap_assert(stream->request);

    ctx = h2_conn_ctx_create_secondary(secondary, stream);
    ctx->task = task = apr_pcalloc(ctx->pool, sizeof(h2_task));
    task->input.beam = stream->input;
    task->output.max_buffer = stream->session->mplx->stream_max_mem;

    apr_table_setn(secondary->notes, H2_TASK_ID_NOTE, ctx->id);

    return task;
}

apr_status_t h2_process_secondary(conn_rec *c, apr_thread_t *thread, int worker_id)
{
    h2_conn_ctx_t *conn_ctx = h2_conn_ctx_get(c);

    ap_assert(conn_ctx);
    ap_assert(conn_ctx->task);

    /* See the discussion at <https://github.com/icing/mod_h2/issues/195>
     *
     * Each conn_rec->id is supposed to be unique at a point in time. Since
     * some modules (and maybe external code) uses this id as an identifier
     * for the request_rec they handle, it needs to be unique for secondary
     * connections also.
     *
     * The MPM module assigns the connection ids and mod_unique_id is using
     * that one to generate identifier for requests. While the implementation
     * works for HTTP/1.x, the parallel execution of several requests per
     * connection will generate duplicate identifiers on load.
     *
     * The original implementation for secondary connection identifiers used
     * to shift the master connection id up and assign the stream id to the
     * lower bits. This was cramped on 32 bit systems, but on 64bit there was
     * enough space.
     *
     * As issue 195 showed, mod_unique_id only uses the lower 32 bit of the
     * connection id, even on 64bit systems. Therefore collisions in request ids.
     *
     * The way master connection ids are generated, there is some space "at the
     * top" of the lower 32 bits on allmost all systems. If you have a setup
     * with 64k threads per child and 255 child processes, you live on the edge.
     *
     * The new implementation shifts 8 bits and XORs in the worker
     * id. This will experience collisions with > 256 h2 workers and heavy
     * load still. There seems to be no way to solve this in all possible
     * configurations by mod_h2 alone.
     */
    c->id = (c->master->id << 8)^worker_id;

    h2_beam_create(&conn_ctx->task->output.beam, conn_ctx->pool, conn_ctx->stream_id, "output",
                   H2_BEAM_OWNER_SEND, 0, c->base_server->timeout);
    if (!conn_ctx->task->output.beam) {
        return APR_ENOMEM;
    }
    
    h2_beam_buffer_size_set(conn_ctx->task->output.beam, conn_ctx->task->output.max_buffer);
    h2_beam_send_from(conn_ctx->task->output.beam, conn_ctx->pool);
    h2_beam_on_consumed(conn_ctx->task->output.beam, NULL, output_consumed, c);

    h2_secondary_run_pre_connection(c, ap_get_conn_socket(c));

    conn_ctx->task->input.bb = apr_brigade_create(conn_ctx->pool, c->bucket_alloc);

    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_task(%s): process connection", conn_ctx->id);
                  
    c->current_thread = thread;
    ap_run_process_connection(c);
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_task(%s): processing done", conn_ctx->id);
    return output_finish(conn_ctx, c);
}

static apr_status_t h2_task_process_secondary(h2_conn_ctx_t *conn_ctx, conn_rec *c)
{
    const h2_request *req = conn_ctx->request;
    conn_state_t *cs = c->cs;
    request_rec *r;

    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_task(%s): create request_rec", conn_ctx->id);
    r = h2_create_request_rec(req, c);
    if (r && (r->status == HTTP_OK)) {
        /* the request_rec->server carries the timeout value that applies */
        h2_beam_timeout_set(conn_ctx->task->output.beam, r->server->timeout);
        if (conn_ctx->task->input.beam) {
            h2_beam_timeout_set(conn_ctx->task->input.beam, r->server->timeout);
        }

        ap_update_child_status(c->sbh, SERVER_BUSY_WRITE, r);
        
        if (cs) {
            cs->state = CONN_STATE_HANDLER;
        }
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_task(%s): start process_request", conn_ctx->id);
    
        /* Add the raw bytes of the request (e.g. header frame lengths to
         * the logio for this request. */
        if (req->raw_bytes && h2_task_logio_add_bytes_in) {
            h2_task_logio_add_bytes_in(c, req->raw_bytes);
        }
        
        ap_process_request(r);
        
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_task(%s): process_request done", conn_ctx->id);
        
        /* After the call to ap_process_request, the
         * request pool may have been deleted.  We set
         * r=NULL here to ensure that any dereference
         * of r that might be added later in this function
         * will result in a segfault immediately instead
         * of nondeterministic failures later.
         */
        if (cs) 
            cs->state = CONN_STATE_WRITE_COMPLETION;
        r = NULL;
    }
    else if (!r) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_task(%s): create request_rec failed, r=NULL", conn_ctx->id);
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_task(%s): create request_rec failed, r->status=%d", 
                      conn_ctx->id, r->status);
    }

    return APR_SUCCESS;
}

static int h2_task_process_conn(conn_rec* c)
{
    h2_conn_ctx_t *ctx;
    
    if (!c->master) {
        return DECLINED;
    }
    
    ctx = h2_conn_ctx_get(c);
    if (ctx->task) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_h2, processing request directly");
        h2_task_process_secondary(ctx, c);
        return DONE;
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c, 
                      "secondary_conn(%ld): has no task", c->id);
    }
    return DECLINED;
}

