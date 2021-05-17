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
#include "h2_frame.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_util.h"

static int h2_session_status_from_apr_status(apr_status_t rv)
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
    h2_session *ctx = (h2_session *)userp;
    size_t written = 0;
    apr_status_t status = h2_io_write(&ctx->io, (const char*)data,
                                      length, &written);
    if (status == APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, ctx->connection,
                      "h2_session: callback send write %d bytes", (int)written);
        return written;
    }
    else if (status == APR_EAGAIN || status == APR_TIMEUP) {
        return NGHTTP2_ERR_WOULDBLOCK;
    }
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, ctx->connection,
                  "h2_session: send error");
    return h2_session_status_from_apr_status(status);
}

static int on_invalid_frame_recv_cb(nghttp2_session *session,
                                    const nghttp2_frame *frame,
                                    uint32_t error_code, void *userp)
{
    h2_session *ctx = (h2_session *)userp;
    if (ctx->loglvl >= APLOG_DEBUG) {
        char buffer[256];
        
        h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                      "h2_session: callback on_invalid_frame_recv error=%d %s",
                      (int)error_code, buffer);
    }
    return 0;
}

static int on_data_chunk_recv_cb(nghttp2_session *session, uint8_t flags,
                                 int32_t stream_id,
                                 const uint8_t *data, size_t len, void *userp)
{
    h2_session *ctx = (h2_session *)userp;
    if (ctx->loglvl >= APLOG_DEBUG) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                      "h2_session: callback on_data_chunk_recv, stream=%d",
                      (int)stream_id);
    }
    return 0;
}

static int before_frame_send_cb(nghttp2_session *session,
                                const nghttp2_frame *frame,
                                void *userp)
{
    h2_session *ctx = (h2_session *)userp;
    if (ctx->loglvl >= APLOG_DEBUG) {
        char buffer[256];
        h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                      "h2_session: before_frame_send %s", buffer);
    }
    return 0;
}

static int on_frame_send_cb(nghttp2_session *session,
                            const nghttp2_frame *frame,
                            void *userp)
{
    h2_session *ctx = (h2_session *)userp;
    apr_status_t status = h2_io_flush(&ctx->io);
    
    if (ctx->loglvl >= APLOG_DEBUG) {
        char buffer[256];
        h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, ctx->connection,
                      "h2_session: callback on_frame_send %s", buffer);
    }
    return 0;
}

static int on_frame_not_send_cb(nghttp2_session *session,
                                const nghttp2_frame *frame,
                                int lib_error_code, void *userp)
{
    h2_session *ctx = (h2_session *)userp;
    if (ctx->loglvl >= APLOG_DEBUG) {
        char buffer[256];
        
        h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                      "h2_session: callback on_frame_not_send error=%d %s",
                      lib_error_code, buffer);
    }
    return 0;
}

static int on_stream_close_cb(nghttp2_session *session, int32_t stream_id,
                              uint32_t error_code, void *userp)
{
    h2_session *ctx = (h2_session *)userp;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                  "h2_session: callback on_stream_close, error=%d",
                  error_code);
    return 0;
}

static int on_begin_headers_cb(nghttp2_session *session,
                               const nghttp2_frame *frame, void *userp)
{
    h2_session *ctx = (h2_session *)userp;
    if (ctx->loglvl >= APLOG_DEBUG) {
        char buffer[256];
        
        h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                      "h2_session: callback on_begin_headers %s",
                      buffer);
    }
    return 0;
}

static int on_header_cb(nghttp2_session *session, const nghttp2_frame *frame,
                        const uint8_t *name, size_t namelen,
                        const uint8_t *value, size_t valuelen,
                        uint8_t flags,
                        void *userp)
{
    h2_session *ctx = (h2_session *)userp;
    if (ctx->loglvl > APLOG_DEBUG) {
        char buffer[256];
        
        h2_util_header_print(buffer, sizeof(buffer)/sizeof(buffer[0]),
                             (const char*)name, namelen,
                             (const char*)value, valuelen);
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, ctx->connection,
                      "h2_session: recv header %s", buffer);
    }
    return 0;
}

static apr_status_t insert_frame_data(h2_session *session,
                                      h2_stream *stream,
                                      const nghttp2_frame *frame)
{
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, stream->c,
                  "h2_stream(%d): headers on stream (eoh=%d, eos=%d)",
                  (int)frame->hd.stream_id,
                  (int)(frame->hd.flags & NGHTTP2_FLAG_END_HEADERS),
                  (int)(frame->hd.flags & NGHTTP2_FLAG_END_STREAM));

    /* Convert the request data (in http/1.1 format) from the frame
     * and place that onto our session->request_data queue.
     */
    const char *data = NULL;
    apr_size_t datalen = 0;
    
    apr_status_t status = h2_frame_to_http(frame, &data, &datalen);
    if (status == APR_SUCCESS && datalen > 0) {
        status = h2_data_queue_push(&session->request_data, data, datalen,
                                    stream);
        
    }
    if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
        stream->eoh = 1;
    }
    if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        stream->state = H2_STREAM_ST_CLOSED_INPUT;
    }
    return status;
}

/**
 * nghttp2 session has received a complete frame. Most, it uses
 * for processing of internal state. HEADER and DATA frames however
 * we need to handle ourself.
 */
static int on_frame_recv_cb(nghttp2_session *ng2s,
                            const nghttp2_frame *frame,
                            void *userp)
{
    h2_session *session = (h2_session *)userp;
    apr_status_t status = APR_SUCCESS;
    
    switch (frame->hd.type) {
        case NGHTTP2_DATA: {
            /* Find the stream this belongs to and push the data
             * into it. */
            const nghttp2_data *data = &frame->data;
            h2_stream * stream = h2_streams_get(&session->streams,
                                                data->hd.stream_id);
            if (stream == NULL) {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, session->connection,
                              "h2_session: data frame for unknown stream %d",
                              (int)data->hd.stream_id);
                return NGHTTP2_ERR_INVALID_STREAM_ID;
            }
            
            if (stream->state != H2_STREAM_ST_OPEN || !stream->eoh) {
                return APR_EINVAL;
            }
            status = insert_frame_data(session, stream, frame);
            break;
        }
        case NGHTTP2_HEADERS: {
            /* This starts a new stream. */
            const nghttp2_headers *headers = &frame->headers;
            h2_stream * stream = NULL;
            status = h2_streams_stream_create(&session->streams, &stream,
                                              headers->hd.stream_id,
                                              &session->request_data);
            if (status != APR_SUCCESS) {
                return NGHTTP2_ERR_INVALID_STREAM_ID;
            }
            if (stream->state != H2_STREAM_ST_IDLE) {
                return APR_EINVAL;
            }
            stream->state = H2_STREAM_ST_OPEN;
            status = insert_frame_data(session, stream, frame);
            break;
        }
        case NGHTTP2_CONTINUATION: {
            const nghttp2_headers *headers = &frame->headers;
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->connection,
                          "h2_session: continue headers on stream %d (eoh=%d, eos=%d)",
                          (int)headers->hd.stream_id,
                          (int)(headers->hd.flags & NGHTTP2_FLAG_END_HEADERS),
                          (int)(headers->hd.flags & NGHTTP2_FLAG_END_STREAM));
            h2_stream * stream = h2_streams_get(&session->streams,
                                                headers->hd.stream_id);
            if (stream == NULL) {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, session->connection,
                              "h2_session: continue frame for unknown stream %d",
                              (int)headers->hd.stream_id);
                return NGHTTP2_ERR_INVALID_STREAM_ID;
            }
            
            if (stream->state != H2_STREAM_ST_OPEN || stream->eoh) {
                return APR_EINVAL;
            }
            status = insert_frame_data(session, stream, frame);
            break;
        }
        default:
            if (session->loglvl >= APLOG_DEBUG) {
                char buffer[256];
                
                h2_util_frame_print(frame, buffer,
                                    sizeof(buffer)/sizeof(buffer[0]));
                ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->connection,
                              "h2_session: on_frame_rcv %s", buffer);
            }
            break;
    }
    
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->connection,
                      "h2_session:error handling frame for stream %d",
                      (int)frame->hd.stream_id);
        return NGHTTP2_ERR_INVALID_STREAM_STATE;
    }
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

static apr_status_t h2_session_create(conn_rec *c, apr_size_t max_streams, h2_session **pctx)
{
    nghttp2_session_callbacks *callbacks = NULL;
    nghttp2_option *options = NULL;
    
    h2_ctx *ctx = h2_ctx_get(c);
    if (!ctx) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, c,
                      "h2_session_serve: h2_ctx missing");
        return APR_EGENERAL;
    }
    
    /* Set up our own context for keeping state of this connection.
     * - initialize the nghttp2 session, with callbacks and options
     * - register the session in our nghttp2_ctx
     */
    h2_session *session = apr_pcalloc(c->pool, sizeof(h2_session));
    session->connection = c;
    session->ngh2 = NULL;
    
    h2_data_queue_init(&session->request_data, c->pool, free);
    h2_streams_init(&session->streams, max_streams, c);
    h2_io_init(c, &session->io);
    
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
    nghttp2_option_set_peer_max_concurrent_streams(options, max_streams);
    
    rv = nghttp2_session_server_new2(&session->ngh2, callbacks,
                                     session, options);
    nghttp2_session_callbacks_del(callbacks);
    nghttp2_option_del(options);
    
    // REALLY?
    *pctx = session;
    ctx->userp = session;
    
    if (rv != 0) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, c,
                      "nghttp2_session_server_new: %s",
                      nghttp2_strerror(rv));
        return APR_EGENERAL;
    }
    return APR_SUCCESS;
}

/**
 * h2_io_on_read_cb implementation that offers the data read
 * directly to the nghttp2 session for consumption.
 */
static apr_status_t session_feed(const char *data, apr_size_t len,
                                 apr_size_t *readlen, int *done,
                                 void *puser)
{
    h2_session *session = (h2_session *)puser;
    if (len > 0) {
        ssize_t n = nghttp2_session_mem_recv(session->ngh2,
                                             (const uint8_t *)data, len);
        if (n < 0) {
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, APR_EGENERAL,
                          session->connection,
                          "h2_session: nghttp2_session_mem_recv error %d",
                          (int)n);
            if (nghttp2_is_fatal(n)) {
                *done = 1;
                return APR_EGENERAL;
            }
        }
        else {
            *readlen = n;
        }
    }
    return APR_SUCCESS;
}

apr_status_t h2_session_serve(conn_rec *c)
{
    h2_session *session = NULL;
    int rv;
    
    apr_status_t status = h2_session_create(c, 32, &session);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, c,
                  "h2_session: new TLS session");
    
    /* Start the conversation by submitting our SETTINGS frame */
    rv = nghttp2_submit_settings(session->ngh2, NGHTTP2_FLAG_NONE, NULL, 0);
    if (rv != 0) {
        status = APR_EGENERAL;
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, c,
                      "nghttp2_submit_settings: %s", nghttp2_strerror(rv));
    }
    else {
        int done = 0;
        while (!done) {
            /* It works like this:
             * - if our http2 engine has something to write, do it.
             * - try to read non-blocking and feed the data to the engine
             * - if there is nothing to write, we switch to blocking reads,
             *   we are a server after all...
             */
            int want_write = nghttp2_session_want_write(session->ngh2);
            if (want_write) {
                rv = nghttp2_session_send(session->ngh2);
                if (rv != 0) {
                    ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, c,
                                  "h2_session: send error %d", rv);
                    done = nghttp2_is_fatal(rv);
                }
            }
            
            if (!done) {
                status = h2_io_read(&session->io, want_write?
                                    APR_NONBLOCK_READ : APR_BLOCK_READ,
                                    session_feed, session);
                switch (status) {
                    case APR_SUCCESS:
                    case APR_EAGAIN:
                        break;
                    case APR_EOF:
                        done = 1;
                        break;
                    default:
                        ap_log_cerror( APLOG_MARK, APLOG_WARNING, status, c,
                                      "h2_session: error reading");
                        done = 1;
                        break;
                }
            }
        }
        
        ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, c,
                      "h2_session: TLS session done, recv returned %d", rv);
    }
    
    nghttp2_session_del(session->ngh2);
    session->ngh2 = NULL;
    
    return status;
}


