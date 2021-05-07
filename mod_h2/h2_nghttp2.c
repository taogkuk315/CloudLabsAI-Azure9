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
    apr_status_t status = h2_io_write(&ctx->io, (const char*)data,
                                      length, &written);
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
    apr_status_t status = h2_io_read(&ctx->io, (char *)buf,
                                     length, &read);
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
    char buffer[256];
    
    h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_frame_rcv %s", buffer);
    return 0;
}

static int on_invalid_frame_recv_cb(nghttp2_session *session,
                                    const nghttp2_frame *frame,
                                    uint32_t error_code, void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    char buffer[256];
    
    h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_invalid_frame_recv error=%d %s",
                  (int)error_code, buffer);
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
    char buffer[256];
    
    h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: before_frame_send %s", buffer);
    return 0;
}

static int on_frame_send_cb(nghttp2_session *session,
                            const nghttp2_frame *frame,
                            void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    char buffer[256];
    
    h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_frame_send %s", buffer);
    return 0;
}

static int on_frame_not_send_cb(nghttp2_session *session,
                                const nghttp2_frame *frame,
                                int lib_error_code, void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    char buffer[256];
    
    h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_frame_not_send error=%d %s",
                  lib_error_code, buffer);
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
    char buffer[256];
    
    h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: callback on_begin_headers %s",
                  buffer);
    return 0;
}

static int on_header_cb(nghttp2_session *session, const nghttp2_frame *frame,
                        const uint8_t *name, size_t namelen,
                        const uint8_t *value, size_t valuelen,
                        uint8_t flags,
                        void *userp)
{
    h2_nghttp2_ctx *ctx = (h2_nghttp2_ctx *)userp;
    char buffer[256];
    
    h2_util_header_print(buffer, sizeof(buffer)/sizeof(buffer[0]),
                         (const char*)name, namelen,
                         (const char*)value, valuelen);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_nghttp2: recv header %s", buffer);
    return 0;
}

#define NGH2_SET_CALLBACK(callbacks, name, fn)\
nghttp2_session_callbacks_set_##name##_callback(callbacks, fn)

static apr_status_t init_callbacks(conn_rec *c, nghttp2_session_callbacks **pcb)
{
    int rv = nghttp2_session_callbacks_new(pcb);
    if (rv != 0) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, c,
                      "nghttp2_session_callbacks_new: %s",
                      nghttp2_strerror(rv));
        return APR_EGENERAL;
    }
    
    NGH2_SET_CALLBACK(*pcb, send, send_cb);
    NGH2_SET_CALLBACK(*pcb, recv, recv_cb);
    NGH2_SET_CALLBACK(*pcb, on_frame_recv, on_frame_recv_cb);
    NGH2_SET_CALLBACK(*pcb, on_invalid_frame_recv, on_invalid_frame_recv_cb);
    NGH2_SET_CALLBACK(*pcb, on_data_chunk_recv, on_data_chunk_recv_cb);
    NGH2_SET_CALLBACK(*pcb, before_frame_send, before_frame_send_cb);
    NGH2_SET_CALLBACK(*pcb, on_frame_send, on_frame_send_cb);
    NGH2_SET_CALLBACK(*pcb, on_frame_not_send, on_frame_not_send_cb);
    NGH2_SET_CALLBACK(*pcb, on_stream_close, on_stream_close_cb);
    NGH2_SET_CALLBACK(*pcb, on_begin_headers, on_begin_headers_cb);
    NGH2_SET_CALLBACK(*pcb, on_header, on_header_cb);
    
    return APR_SUCCESS;
}

static apr_status_t h2_nghttp2_ctx_create(conn_rec *c, h2_nghttp2_ctx **pctx)
{
    nghttp2_session_callbacks *callbacks = NULL;
    nghttp2_option *options = NULL;

    h2_ctx *ctx = h2_ctx_get(c);
    if (!ctx) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, c,
                      "h2_nghttp2_serve: h2_ctx missing");
        return APR_EGENERAL;
    }
    
    /* Set up our own context for keeping state of this connection.
     * - initialize the nghttp2 session, with callbacks and options
     * - register the session in our nghttp2_ctx
     */
    h2_nghttp2_ctx *h2ng_ctx = apr_pcalloc(c->pool, sizeof(h2_nghttp2_ctx));
    h2ng_ctx->connection = c;
    h2ng_ctx->session = NULL;
    h2_io_init(c, &h2ng_ctx->io);
    
    apr_status_t status = init_callbacks(c, &callbacks);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    int rv = nghttp2_option_new(&options);
    if (rv != 0) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, c,
                      "nghttp2_option_new: %s", nghttp2_strerror(rv));
        return APR_EGENERAL;
    }
    
    /* Our server nghttp2 options
     * TODO: some should come from config
     */
    nghttp2_option_set_recv_client_preface(options, 1);
    nghttp2_option_set_peer_max_concurrent_streams(options, 100);
    
    rv = nghttp2_session_server_new2(&h2ng_ctx->session, callbacks,
                                     h2ng_ctx, options);
    nghttp2_session_callbacks_del(callbacks);
    nghttp2_option_del(options);

    // REALLY?
    *pctx = h2ng_ctx;
    ctx->userp = h2ng_ctx;
    
    if (rv != 0) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, c,
                      "nghttp2_session_server_new: %s",
                      nghttp2_strerror(rv));
        return APR_EGENERAL;
    }
    return APR_SUCCESS;
}

apr_status_t h2_nghttp2_serve(conn_rec *c)
{
    h2_nghttp2_ctx *h2ng_ctx = NULL;
    int rv;
    
    apr_status_t status = h2_nghttp2_ctx_create(c, &h2ng_ctx);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, c,
                  "h2_nghttp2: new TLS session");
    
    /* Start the conversation by submitting our SETTINGS frame */
    rv = nghttp2_submit_settings(h2ng_ctx->session, NGHTTP2_FLAG_NONE, NULL, 0);
    if (rv != 0) {
        status = APR_EGENERAL;
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, c,
                      "nghttp2_submit_settings: %s", nghttp2_strerror(rv));
    }
    else {
        // Receive frames from client
        char buffer[16 * 1024];
        size_t length = sizeof(buffer)/sizeof(buffer[0]);
        size_t read, offset;
        apr_status_t status;
        int done = 0;
        
        while (!done && !nghttp2_is_fatal(rv)) {
            if (nghttp2_session_want_write(h2ng_ctx->session)) {
                rv = nghttp2_session_send(h2ng_ctx->session);
                if (rv != 0) {
                    ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, c,
                      "h2_nghttp2: send error %d", rv);
                }
            }
            
            if (read <= offset) {
                read = offset = 0;
                status = h2_io_read(&h2ng_ctx->io, buffer, length, &read);
                switch (status) {
                    case APR_SUCCESS:
                    case APR_EAGAIN:
                        break;
                    case APR_EOF:
                        done = 1;
                        break;
                    default:
                        ap_log_cerror( APLOG_MARK, APLOG_WARNING, status, c,
                                      "h2_nghttp2: error reading");
                        done = 1;
                        break;
                }
            }
        
            if (read > offset) {
                /*char scratch[256];
                h2_util_hex_dump(scratch, sizeof(scratch)/sizeof(scratch[0]),
                                 buffer, read);
                ap_log_cerror( APLOG_MARK, APLOG_TRACE2, 0, c,
                              "h2_nghttp2: read %d bytes [%s]",
                              (int)read, scratch);
                 */
                ssize_t n = nghttp2_session_mem_recv(h2ng_ctx->session,
                                                     (const uint8_t *)buffer+offset,
                                                     read-offset);
                if (n < 0) {
                    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, APR_EGENERAL, c,
                        "h2_nghttp2: recv error %d", (int)n);
                    rv = n;
                }
                else if (n < read) {
                    offset += n;
                }
                else {
                    offset = read = 0;
                }
            }
        }
        
        ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, c,
                      "h2_nghttp2: TLS session done, recv returned %d", rv);
    }
    
    nghttp2_session_del(h2ng_ctx->session);
    h2ng_ctx->session = NULL;
    
    return status;
}
