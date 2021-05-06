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

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_ctx.h"
#include "h2_nghttp2.h"
#include "h2_util.h"

static int h2_nghttp2_status_from_apr_status(apr_status_t rv)
{
    switch (rv) {
        case APR_SUCCESS:
            return NGHTTP2_NO_ERROR;
        case APR_EAGAIN:
        case APR_TIMEUP:
            return NGHTTP2_ERR_WOULDBLOCK;
        case APR_EOF:
            return NGHTTP2_ERR_EOF;
        default:
            return NGHTTP2_ERR_PROTO;
    }
}

/*
 * Callback when nghttp2 wants to send bytes back to the client.
 */
static ssize_t send_cb(nghttp2_session *session,
                       const uint8_t *data, size_t length,
                       int flags, void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    size_t written = 0;
    apr_status_t status = h2_io_write(&ctx->io, data, length, &written);
    if (status == APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                      "h2_nghttp2: callback send write %d bytes", (int)written);
        return written;
    }
    else if (status == APR_EAGAIN || status == APR_TIMEUP) {
        return NGHTTP2_ERR_WOULDBLOCK;
    }
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, ctx->connection,
                  "h2_nghttp2: send error");
    return h2_nghttp2_status_from_apr_status(status);
}

static ssize_t recv_cb(nghttp2_session *session,
                       uint8_t *buf, size_t length,
                       int flags, void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    size_t read = 0;
    apr_status_t status = h2_io_read(&ctx->io, buf, length, &read);
    if (status == APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                      "h2_nghttp2: callback recv %d bytes", (int)read);
        return read;
    }
    else if (APR_STATUS_IS_EOF(status)) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, ctx->connection,
                      "h2_nghttp2: callback recv eof");
        return NGHTTP2_ERR_EOF;
    }
    else if (status == APR_EAGAIN || status == APR_TIMEUP) {
        return NGHTTP2_ERR_WOULDBLOCK;
    }
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, ctx->connection,
                  "h2_nghttp2: callback recv error");
    return h2_nghttp2_status_from_apr_status(status);
}

static int on_frame_recv_cb(nghttp2_session *session,
                            const nghttp2_frame *frame,
                            void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_frame_rcv, type=%d",
                  frame->hd.type);
    return 0;
}

static int on_invalid_frame_recv_cb(nghttp2_session *session,
                                    const nghttp2_frame *frame,
                                    uint32_t error_code, void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_invalid_frame_recv, type=%d",
                  frame->hd.type);
    return 0;
}

static int on_data_chunk_recv_cb(nghttp2_session *session, uint8_t flags,
                                 int32_t stream_id,
                                 const uint8_t *data, size_t len, void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_data_chunk_recv, stream=%d",
                  (int)stream_id);
    return 0;
}

static int before_frame_send_cb(nghttp2_session *session,
                                const nghttp2_frame *frame,
                                void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: before_frame_send, type=%d",
                  frame->hd.type);
    return 0;
}

static int on_frame_send_cb(nghttp2_session *session,
                            const nghttp2_frame *frame,
                            void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_frame_send, type=%d",
                  frame->hd.type);
    return 0;
}

static int on_frame_not_send_cb(nghttp2_session *session,
                                const nghttp2_frame *frame,
                                int lib_error_code, void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_frame_not_send, error=%d",
                  lib_error_code);
    return 0;
}

static int on_stream_close_cb(nghttp2_session *session, int32_t stream_id,
                              uint32_t error_code, void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_stream_close, error=%d",
                  error_code);
    return 0;
}

static int on_begin_headers_cb(nghttp2_session *session,
                               const nghttp2_frame *frame, void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_begin_headers, type=%d",
                  frame->hd.type);
    return 0;
}

static int on_header_cb(nghttp2_session *session, const nghttp2_frame *frame,
                        const uint8_t *name, size_t namelen,
                        const uint8_t *value, size_t valuelen,
                        uint8_t flags,
                        void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_header, namelen=%d",
                  (int)namelen);
    return 0;
}

#define NGH2_SET_CALLBACK(callbacks, name, fn)\
nghttp2_session_callbacks_set_##name##_callback(callbacks, fn)

static int init_callbacks(conn_rec *c, nghttp2_session_callbacks **pcb)
{
    int rv = nghttp2_session_callbacks_new(pcb);
    if (rv != 0) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, c,
                      "nghttp2_session_callbacks_new: %s",
                      nghttp2_strerror(rv));
        return APR_EGENERAL;
    }
    
    NGH2_SET_CALLBACK(*pcb, send, send_cb);
    /*NGH2_SET_CALLBACK(*pcb, recv, recv_cb);*/
    NGH2_SET_CALLBACK(*pcb, on_frame_recv, on_frame_recv_cb);
    NGH2_SET_CALLBACK(*pcb, on_invalid_frame_recv, on_invalid_frame_recv_cb);
    NGH2_SET_CALLBACK(*pcb, on_data_chunk_recv, on_data_chunk_recv_cb);
    NGH2_SET_CALLBACK(*pcb, before_frame_send, before_frame_send_cb);
    NGH2_SET_CALLBACK(*pcb, on_frame_send, on_frame_send_cb);
    NGH2_SET_CALLBACK(*pcb, on_frame_not_send, on_frame_not_send_cb);
    NGH2_SET_CALLBACK(*pcb, on_stream_close, on_stream_close_cb);
    NGH2_SET_CALLBACK(*pcb, on_begin_headers, on_begin_headers_cb);
    NGH2_SET_CALLBACK(*pcb, on_header, on_header_cb);
    
    return OK;
}

apr_status_t h2_nghttp2_serve(conn_rec *c)
{
    apr_status_t status = APR_SUCCESS;
    nghttp2_session * session = NULL;
    nghttp2_session_callbacks *callbacks = NULL;
    
    h2_ctx *ctx = h2_ctx_get(c);
    if (!ctx) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, c,
                      "h2_nghttp2_serve: h2_ctx missing");
        return APR_EGENERAL;
    }
    
    int rv = init_callbacks(c, &callbacks);
    if (rv != 0) {
        return rv;
    }
    
    h2_nghttp2_ctx *h2ng_ctx = apr_pcalloc(c->pool, sizeof(h2_nghttp2_ctx));
    h2ng_ctx->connection = c;
    h2ng_ctx->session = NULL;
    h2_io_init(c, &h2ng_ctx->io);
    
    rv = nghttp2_session_server_new(&h2ng_ctx->session, callbacks, h2ng_ctx);
    nghttp2_session_callbacks_del(callbacks);
    
    if (rv != 0) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, c,
                      "nghttp2_session_server_new: %s",
                      nghttp2_strerror(rv));
        return APR_EGENERAL;
    }
    
    // Now we need to handle the traffic
    ctx->userp = h2ng_ctx;
    ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, c,
                  "h2_nghttp2: new TLS session");
    
    //rv = nghttp2_submit_settings(h2ng_ctx->session, NGHTTP2_FLAG_NONE, NULL, 0);
    if (rv != 0) {
        status = APR_EGENERAL;
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, c,
                      "nghttp2_submit_settings: %s", nghttp2_strerror(rv));
    }
    else {
        // Receive frames from client
        unsigned char buffer[16 * 1024];
        size_t length = sizeof(buffer)/sizeof(buffer[0]);
        
        while (!nghttp2_is_fatal(rv)) {
            if (nghttp2_session_want_write(h2ng_ctx->session)) {
                rv = nghttp2_session_send(h2ng_ctx->session);
                if (rv != 0) {
                    ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, c,
                      "h2_nghttp2: send error %d", rv);
                }
            }
            
            size_t read = 0;
            apr_status_t status = h2_io_read(&h2ng_ctx->io, buffer, length,
                                             &read);
            if (read > 0) {
                ssize_t processed =
                    nghttp2_session_mem_recv(h2ng_ctx->session,
                                             (const uint8_t *)buffer, read);
                if (processed < 0) {
                    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c,
                        "h2_nghttp2: recv error %d", rv);
                }
                else if (processed < read) {
                    ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, c,
                        "h2_nghttp2: recv has not processed all bytes");
                }
            }
            else if (status == APR_EOF) {
                break;
            }
        }
        
        ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, c,
                      "h2_nghttp2: TLS session done, recv returned %d", rv);
    }
    
    nghttp2_session_del(h2ng_ctx->session);
    h2ng_ctx->session = NULL;
    
    return status;
}
