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
#include <apr_base64.h>
#include <apr_base64.h>

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_config.h"
#include "h2_bucket.h"
#include "h2_mplx.h"
#include "h2_response.h"
#include "h2_stream.h"
#include "h2_stream_set.h"
#include "h2_from_h1.h"
#include "h2_task.h"
#include "h2_bucket.h"
#include "h2_session.h"
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

static int stream_open(h2_session *session, int stream_id)
{
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    h2_stream * stream = h2_stream_create(stream_id, session->pool,
                                          session->mplx);
    if (!stream) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_ENOMEM, session->c,
                      "h2_session: stream(%ld-%d): unable to create",
                      session->id, stream_id);
        return NGHTTP2_ERR_INVALID_STREAM_ID;
    }
    
    apr_status_t status = h2_stream_set_add(session->streams, stream);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, session->c,
                      "h2_session: stream(%ld-%d): unable to add to pool",
                      session->id, h2_stream_get_id(stream));
        return NGHTTP2_ERR_INVALID_STREAM_ID;
    }
    
    stream->state = H2_STREAM_ST_OPEN;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                  "h2_session: stream(%ld-%d): opened",
                  session->id, stream_id);
    
    h2_mplx_start_io(session->mplx, stream_id);
    return 0;
}

static apr_status_t stream_end_headers(h2_session *session,
                                       h2_stream *stream, int eos)
{
    apr_status_t status = h2_stream_write_eoh(stream);
    if (status == APR_SUCCESS) {
        if (eos) {
            status = h2_stream_write_eos(stream);
        }
        
        if (status == APR_SUCCESS && session->after_stream_opened_cb) {
            h2_task *task = h2_stream_create_task(stream, session->c);
            session->after_stream_opened_cb(session, stream, task);
        }
    }
    return status;
}


/*
 * Callback when nghttp2 wants to send bytes back to the client.
 */
static ssize_t send_cb(nghttp2_session *ngh2,
                       const uint8_t *data, size_t length,
                       int flags, void *userp)
{
    h2_session *session = (h2_session *)userp;
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    
    size_t written = 0;
    apr_status_t status = h2_conn_io_write(&session->io, (const char*)data,
                                      length, &written);
    if (status == APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE3, 0, session->c,
                      "h2_session: callback send write %d bytes", (int)written);
        return written;
    }
    else if (status == APR_EAGAIN || status == APR_TIMEUP) {
        return NGHTTP2_ERR_WOULDBLOCK;
    }
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, session->c,
                  "h2_session: send error");
    return h2_session_status_from_apr_status(status);
}

static int on_invalid_frame_recv_cb(nghttp2_session *ngh2,
                                    const nghttp2_frame *frame,
                                    uint32_t error_code, void *userp)
{
    h2_session *session = (h2_session *)userp;
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    if (session->loglvl >= APLOG_DEBUG) {
        char buffer[256];
        
        h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                      "h2_session: callback on_invalid_frame_recv error=%d %s",
                      (int)error_code, buffer);
    }
    return 0;
}

static int on_data_chunk_recv_cb(nghttp2_session *ngh2, uint8_t flags,
                                 int32_t stream_id,
                                 const uint8_t *data, size_t len, void *userp)
{
    h2_session *session = (h2_session *)userp;
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    h2_stream * stream = h2_stream_set_get(session->streams, stream_id);
    if (!stream) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, session->c,
                      "h2_session:  stream(%ld-%d): on_data_chunk for unknown stream",
                      session->id, (int)stream_id);
        return NGHTTP2_ERR_INVALID_STREAM_ID;
    }
    
    apr_status_t status = h2_stream_write_data(stream, (const char *)data, len);
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, session->c,
                  "h2_stream(%ld-%d): written DATA, length %ld",
                  session->id, stream_id, len);
    return (status == APR_SUCCESS)? 0 : NGHTTP2_ERR_PROTO;
}

static int before_frame_send_cb(nghttp2_session *ngh2,
                                const nghttp2_frame *frame,
                                void *userp)
{
    h2_session *session = (h2_session *)userp;
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    if (session->loglvl >= APLOG_DEBUG) {
        char buffer[256];
        h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                      "h2_session: before_frame_send %s", buffer);
    }
    return 0;
}

static int on_frame_send_cb(nghttp2_session *ngh2,
                            const nghttp2_frame *frame,
                            void *userp)
{
    h2_session *session = (h2_session *)userp;
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return 0;
}

static int on_frame_not_send_cb(nghttp2_session *ngh2,
                                const nghttp2_frame *frame,
                                int lib_error_code, void *userp)
{
    h2_session *session = (h2_session *)userp;
    if (session->loglvl >= APLOG_DEBUG) {
        char buffer[256];
        
        h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                      "h2_session: callback on_frame_not_send error=%d %s",
                      lib_error_code, buffer);
    }
    return 0;
}

static apr_status_t close_active_stream(h2_session *session,
                                        h2_stream *stream,
                                        int join)
{
    apr_status_t status = APR_SUCCESS;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                  "h2_stream(%ld-%d): closing",
                  session->id, (int)stream->id);
    
    h2_stream_set_remove(session->streams, stream);
    if (session->before_stream_close_cb) {
        status = session->before_stream_close_cb(session, stream,
                                                 stream->task, join);
    }
    if (status == APR_SUCCESS) {
        h2_mplx_end_io(session->mplx, stream->id);
        h2_stream_destroy(stream);
    }
    else if (status == APR_EAGAIN) {
        h2_stream_set_add(session->zombies, stream);
    }
    return status;
}

static apr_status_t join_zombie_stream(h2_session *session, h2_stream *stream)
{
    apr_status_t status = APR_SUCCESS;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                  "h2_stream(%ld-%d): join zombie",
                  session->id, (int)stream->id);
    
    h2_stream_set_remove(session->zombies, stream);
    if (session->before_stream_close_cb) {
        status = session->before_stream_close_cb(session, stream,
                                                 stream->task, 1);
    }
    h2_stream_destroy(stream);
    return status;
}

static int on_stream_close_cb(nghttp2_session *ngh2, int32_t stream_id,
                              uint32_t error_code, void *userp)
{
    h2_session *session = (h2_session *)userp;
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    h2_stream *stream = h2_stream_set_get(session->streams, stream_id);
    if (stream) {
        apr_status_t status = close_active_stream(session, stream, 0);
    }
    
    if (error_code) {
        ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, session->c,
                      "h2_stream(%ld-%d): close error %d",
                      session->id, (int)stream_id, error_code);
    }
    
    return 0;
}

static int on_begin_headers_cb(nghttp2_session *ngh2,
                               const nghttp2_frame *frame, void *userp)
{
    /* This starts a new stream. */
    return stream_open((h2_session *)userp, frame->hd.stream_id);
}

static int on_header_cb(nghttp2_session *ngh2, const nghttp2_frame *frame,
                        const uint8_t *name, size_t namelen,
                        const uint8_t *value, size_t valuelen,
                        uint8_t flags,
                        void *userp)
{
    h2_session *session = (h2_session *)userp;
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    h2_stream * stream = h2_stream_set_get(session->streams,
                                           frame->hd.stream_id);
    if (!stream) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, session->c,
                      "h2_session:  stream(%ld-%d): on_header for unknown stream",
                      session->id, (int)frame->hd.stream_id);
        return NGHTTP2_ERR_INVALID_STREAM_ID;
    }
    
    apr_status_t status = h2_stream_write_header(stream,
                                               (const char *)name, namelen,
                                               (const char *)value, valuelen);
    return (status == APR_SUCCESS)? 0 : NGHTTP2_ERR_PROTO;
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
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    apr_status_t status = APR_SUCCESS;
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, session->c,
                  "h2_session(%ld): on_frame_rcv, type=%d", session->id,
                  frame->hd.type);
    switch (frame->hd.type) {
        case NGHTTP2_HEADERS: {
            h2_stream * stream = h2_stream_set_get(session->streams,
                                                   frame->hd.stream_id);
            if (stream == NULL) {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, session->c,
                              "h2_session:  stream(%ld-%d): HEADERS frame "
                              "for unknown stream", session->id,
                              (int)frame->hd.stream_id);
                return NGHTTP2_ERR_INVALID_STREAM_ID;
            }
            
            if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
                int eos = (frame->hd.flags & NGHTTP2_FLAG_END_STREAM);
                status = stream_end_headers(session, stream, eos);
            }
            break;
        }
        case NGHTTP2_DATA: {
            h2_stream * stream = h2_stream_set_get(session->streams,
                                                   frame->hd.stream_id);
            if (stream == NULL) {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, session->c,
                              "h2_session:  stream(%ld-%d): DATA frame "
                              "for unknown stream", session->id,
                              (int)frame->hd.stream_id);
                return NGHTTP2_ERR_PROTO;
            }
            break;
        }
        default:
            if (session->loglvl >= APLOG_DEBUG) {
                char buffer[256];
                
                h2_util_frame_print(frame, buffer,
                                    sizeof(buffer)/sizeof(buffer[0]));
                ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                              "h2_session: on_frame_rcv %s", buffer);
            }
            break;
    }
    
    if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        h2_stream * stream = h2_stream_set_get(session->streams,
                                               frame->hd.stream_id);
        if (stream != NULL) {
            status = h2_stream_write_eos(stream);
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, session->c,
                          "h2_stream(%ld-%d): input closed",
                          session->id, (int)frame->hd.stream_id);
        }
    }
    
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                      "h2_session: stream(%ld-%d): error handling frame",
                      session->id, (int)frame->hd.stream_id);
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

static h2_session *h2_session_create_int(conn_rec *c,
                                         request_rec *r,
                                         h2_config *config)
{
    nghttp2_session_callbacks *callbacks = NULL;
    nghttp2_option *options = NULL;
    
    apr_allocator_t *allocator = NULL;
    apr_status_t status = apr_allocator_create(&allocator);
    if (status != APR_SUCCESS) {
        return NULL;
    }
    
    apr_pool_t *pool = NULL;
    status = apr_pool_create_ex(&pool, c->pool, NULL, allocator);
    if (status != APR_SUCCESS) {
        return NULL;
    }

    h2_session *session = apr_pcalloc(pool, sizeof(h2_session));
    if (session) {
        session->id = c->id;
        
        session->allocator = allocator;
        session->pool = pool;
        status = apr_thread_mutex_create(&session->alock, 
                                         APR_THREAD_MUTEX_DEFAULT,
                                         session->pool);
        apr_allocator_mutex_set(session->allocator, session->alock);
        
        session->c = c;
        session->r = r;
        session->ngh2 = NULL;
        session->loglvl = APLOGcdebug(c)? APLOG_DEBUG : APLOG_NOTICE;
        
        session->streams = h2_stream_set_create(session->pool);
        session->zombies = h2_stream_set_create(session->pool);
        
        session->mplx = h2_mplx_create(c, session->pool);
        
        h2_conn_io_init(&session->io, c);
        
        apr_status_t status = init_callbacks(c, &callbacks);
        if (status != APR_SUCCESS) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, status, c,
                          "nghttp2: error in init_callbacks");
            h2_session_destroy(session);
            return NULL;
        }
        
        int rv = nghttp2_option_new(&options);
        if (rv != 0) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, status, c,
                          "nghttp2_option_new: %s", nghttp2_strerror(rv));
            h2_session_destroy(session);
            return NULL;
        }

        /* With a request present, we are in 'h2c' mode and 
         * expect a preface from the client. */
        nghttp2_option_set_recv_client_preface(options, 1);
        
        nghttp2_option_set_peer_max_concurrent_streams(
            options, h2_config_geti(config, H2_CONF_MAX_STREAMS));
        
        rv = nghttp2_session_server_new2(&session->ngh2, callbacks,
                                         session, options);
        nghttp2_session_callbacks_del(callbacks);
        nghttp2_option_del(options);
        
        if (rv != 0) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, c,
                          "nghttp2_session_server_new: %s",
                          nghttp2_strerror(rv));
            h2_session_destroy(session);
            return NULL;
        }
        
    }
    return session;
}

static int stream_close_finished(void *ctx, h2_stream *stream) {
    assert(ctx);
    h2_session *session = (h2_session *)ctx;
    h2_task *task = stream->task;
    if (task && h2_task_has_finished(task)) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, session->c,
                      "h2_session(%ld): reaping zombie stream(%d)",
                      session->id, stream->id);
        h2_stream_set_remove(session->zombies, stream);
        h2_stream_destroy(stream);
    }
    return 1;
}

static void reap_zombies(h2_session *session) {
    if (session->zombies) {
        /* remove all zombies, where the task has run */
        h2_stream_set_iter(session->zombies, stream_close_finished, session);
    }
}

h2_session *h2_session_create(conn_rec *c, h2_config *config)
{
    return h2_session_create_int(c, NULL, config);
}

h2_session *h2_session_rcreate(request_rec *r, h2_config *config)
{
    return h2_session_create_int(r->connection, r, config);
}

static int close_active_iter(void *ctx, h2_stream *stream) {
    assert(ctx);
    close_active_stream((h2_session *)ctx, stream, 1);
    return 1;
}

static int close_zombie_iter(void *ctx, h2_stream *stream) {
    assert(ctx);
    join_zombie_stream((h2_session *)ctx, stream);
    return 1;
}

void h2_session_destroy(h2_session *session)
{
    assert(session);
    if (session->streams) {
        if (h2_stream_set_size(session->streams)) {
            ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, session->c,
                          "h2_session(%ld): destroy, %ld streams open",
                          session->id, h2_stream_set_size(session->streams));
            /* destroy all sessions, join all existing tasks */
            h2_stream_set_iter(session->streams, close_active_iter, session);
            ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, session->c,
                          "h2_session(%ld): destroy, %ld streams remain",
                          session->id, h2_stream_set_size(session->streams));
        }
        h2_stream_set_destroy(session->streams);
        session->streams = NULL;
    }
    if (session->zombies) {
        if (h2_stream_set_size(session->zombies)) {
            ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, session->c,
                          "h2_session(%ld): destroy, %ld zombie streams",
                          session->id, h2_stream_set_size(session->zombies));
            /* destroy all zombies, join all existing tasks */
            h2_stream_set_iter(session->zombies, close_zombie_iter, session);
            ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, session->c,
                          "h2_session(%ld): destroy, %ld zombies remain",
                          session->id, h2_stream_set_size(session->zombies));
        }
        h2_stream_set_destroy(session->zombies);
        session->zombies = NULL;
    }
    if (session->ngh2) {
        nghttp2_session_del(session->ngh2);
        session->ngh2 = NULL;
    }
    if (session->mplx) {
        h2_mplx_destroy(session->mplx);
        session->mplx = NULL;
    }
    h2_conn_io_destroy(&session->io);
    
    apr_allocator_t *allocator = session->allocator;
    if (session->alock) {
        if (allocator) {
            apr_allocator_mutex_set(allocator, session->alock);
        }
        apr_thread_mutex_destroy(session->alock);
        session->alock = NULL;
    }
    if (session->pool) {
        apr_pool_destroy(session->pool);
    }
    if (allocator) {
        apr_allocator_destroy(allocator);
    }
}

apr_status_t h2_session_goaway(h2_session *session, apr_status_t reason)
{
    assert(session);
    apr_status_t status = APR_SUCCESS;
    if (session->aborted) {
        return APR_EINVAL;
    }
    
    int rv = 0;
    if (reason == APR_SUCCESS) {
        rv = nghttp2_submit_shutdown_notice(session->ngh2);
    }
    else {
        int err = 0;
        int last_id = nghttp2_session_get_last_proc_stream_id(session->ngh2);
        rv = nghttp2_submit_goaway(session->ngh2, last_id,
                                   NGHTTP2_FLAG_NONE, err, NULL, 0);
    }
    if (rv != 0) {
        status = APR_EGENERAL;
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                      "session(%ld): submit goaway: %s",
                      session->id, nghttp2_strerror(rv));
    }
    return status;
}

static apr_status_t h2_session_abort_int(h2_session *session, int reason)
{
    assert(session);
    if (!session->aborted) {
        session->aborted = 1;
        nghttp2_session_terminate_session(session->ngh2, reason);
        h2_mplx_abort(session->mplx);
    }
    return APR_SUCCESS;
}

apr_status_t h2_session_abort(h2_session *session, apr_status_t reason)
{
    assert(session);
    int err = NGHTTP2_ERR_PROTO;
    switch (reason) {
        case APR_ENOMEM:
            err = NGHTTP2_ERR_NOMEM;
            break;
        case APR_EOF:
            err = 0;
            break;
        case APR_ECONNABORTED:
            err = NGHTTP2_ERR_EOF;
            break;
        default:
            break;
    }
    return h2_session_abort_int(session, err);
}

apr_status_t h2_session_start(h2_session *session)
{
    assert(session);
    /* Start the conversation by submitting our SETTINGS frame */
    apr_status_t status = APR_SUCCESS;
    h2_config *config = h2_config_get(session->c);
    int rv = 0;
    
    if (session->r) {
        /* 'h2c' mode: we should have a 'HTTP2-Settings' header with
         * base64 encoded client settings. */
        const char *s = apr_table_get(session->r->headers_in, "HTTP2-Settings");
        if (!s) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, status, session->r,
                          "HTTP2-Settings header missing in request");
            return APR_EINVAL;
        }
        int cslen = apr_base64_decode_len(s);
        char *cs = apr_pcalloc(session->r->pool, cslen);
        --cslen; /* apr also counts the terminating 0 */
        apr_base64_decode(cs, s);
        
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, session->r,
                      "upgrading h2c session with nghttp2 from %s (%d)",
                      s, cslen);
        
        rv = nghttp2_session_upgrade(session->ngh2, (uint8_t*)cs, cslen, NULL);
        if (rv != 0) {
            status = APR_EGENERAL;
            ap_log_rerror(APLOG_MARK, APLOG_ERR, status, session->r,
                          "nghttp2_session_upgrade: %s", nghttp2_strerror(rv));
            return status;
        }
        
        /* Now we need to auto-open stream 1 for the request we got. */
        rv = stream_open(session, 1);
        if (rv != 0) {
            status = APR_EGENERAL;
            ap_log_rerror(APLOG_MARK, APLOG_ERR, status, session->r,
                          "open stream 1: %s", nghttp2_strerror(rv));
            return status;
        }
        
        h2_stream * stream = h2_stream_set_get(session->streams, 1);
        if (stream == NULL) {
            status = APR_EGENERAL;
            ap_log_rerror(APLOG_MARK, APLOG_ERR, status, session->r,
                          "lookup of stream 1");
            return status;
        }
        
        status = h2_stream_rwrite(stream, session->r);
        if (status != APR_SUCCESS) {
            return status;
        }
        status = stream_end_headers(session, stream, 1);
        if (status != APR_SUCCESS) {
            return status;
        }
        status = h2_stream_write_eos(stream);
        if (status != APR_SUCCESS) {
            return status;
        }
    }
    
    nghttp2_settings_entry settings[] = {
        { NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
            h2_config_geti(config, H2_CONF_MAX_HL_SIZE) },
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
            h2_config_geti(config, H2_CONF_WIN_SIZE) },
    };
    rv = nghttp2_submit_settings(session->ngh2, NGHTTP2_FLAG_NONE,
                                 settings,
                                 sizeof(settings)/sizeof(settings[0]));
    if (rv != 0) {
        status = APR_EGENERAL;
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                      "nghttp2_submit_settings: %s", nghttp2_strerror(rv));
    }
    return status;
}

static int h2_session_want_read(h2_session *session)
{
    assert(session);
    return nghttp2_session_want_read(session->ngh2);
}

static int h2_session_want_write(h2_session *session)
{
    assert(session);
    return nghttp2_session_want_write(session->ngh2);
}

static h2_stream *resume_on_data(void *ctx, h2_stream *stream) {
    h2_session *session = (h2_session *)ctx;
    assert(session);
    assert(stream);
    
    if (h2_stream_is_suspended(stream)) {
        ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, stream->pool,
                      "h2_stream(%ld-%d): suspended, checking for DATA",
                      h2_mplx_get_id(stream->m), stream->id);
        if (h2_mplx_out_has_data_for(stream->m, h2_stream_get_id(stream))) {
            h2_stream_set_suspended(stream, 0);
            int rv = nghttp2_session_resume_data(session->ngh2,
                                                 h2_stream_get_id(stream));
            ap_log_cerror(APLOG_MARK, nghttp2_is_fatal(rv)?
                          APLOG_ERR : APLOG_DEBUG, 0, session->c,
                          "h2_stream(%ld-%d): resuming stream %s",
                          session->id, stream->id, nghttp2_strerror(rv));
        }
    }
    return NULL;
}

static void h2_session_resume_streams_with_data(h2_session *session) {
    assert(session);
    if (!h2_stream_set_is_empty(session->streams)
        && session->mplx && !session->aborted) {
        /* Resume all streams where we have data in the out queue and
         * which had been suspended before. */
        h2_stream_set_find(session->streams, resume_on_data, session);
    }
}

apr_status_t h2_session_write(h2_session *session, apr_interval_time_t timeout)
{
    assert(session);
    apr_status_t status = APR_SUCCESS;
    int have_written = 0;
    
    /* First check if we have new streams to submit */
    for (h2_response *head = h2_session_pop_response(session); head;
         head = h2_session_pop_response(session)) {
        h2_stream *stream = h2_session_get_stream(session, head->stream_id);
        if (stream) {
            status = h2_session_handle_response(session, stream, head);
            have_written = 1;
        }
        h2_response_destroy(head);
    }
    
    h2_session_resume_streams_with_data(session);
    
    if (!have_written && timeout > 0 && !h2_session_want_write(session)) {
        status = h2_mplx_out_trywait(session->mplx, timeout);
        if (status != APR_TIMEUP) {
            h2_session_resume_streams_with_data(session);
        }
    }
    
    if (h2_session_want_write(session)) {
        int rv = nghttp2_session_send(session->ngh2);
        if (rv != 0) {
            ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, session->c,
                          "h2_session: send error %d", rv);
            if (nghttp2_is_fatal(rv)) {
                h2_session_abort_int(session, rv);
                status = APR_EGENERAL;
            }
        }
        status = h2_conn_io_flush(&session->io);
    }
    
    reap_zombies(session);

    return status;
}

h2_stream *h2_session_get_stream(h2_session *session, int stream_id)
{
    assert(session);
    return h2_stream_set_get(session->streams, stream_id);
}

/* h2_io_on_read_cb implementation that offers the data read
 * directly to the session for consumption.
 */
static apr_status_t session_receive(const char *data, apr_size_t len,
                                    apr_size_t *readlen, int *done,
                                    void *puser)
{
    h2_session *session = (h2_session *)puser;
    assert(session);
    if (len > 0) {
        ssize_t n = nghttp2_session_mem_recv(session->ngh2,
                                             (const uint8_t *)data, len);
        if (n < 0) {
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, APR_EGENERAL,
                          session->c,
                          "h2_session: nghttp2_session_mem_recv error %d",
                          (int)n);
            if (nghttp2_is_fatal(n)) {
                *done = 1;
                h2_session_abort_int(session, n);
                return APR_EGENERAL;
            }
        }
        else {
            *readlen = n;
        }
    }
    return APR_SUCCESS;
}

apr_status_t h2_session_read(h2_session *session, apr_read_type_e block)
{
    assert(session);
    return h2_conn_io_read(&session->io, block, session_receive, session);
}

apr_status_t h2_session_close(h2_session *session)
{
    assert(session);
    return h2_conn_io_flush(&session->io);
}

void h2_session_set_stream_close_cb(h2_session *session, before_stream_close *cb)
{
    assert(session);
    session->before_stream_close_cb = cb;
}

void h2_session_set_stream_open_cb(h2_session *session, after_stream_open *cb)
{
    assert(session);
    session->after_stream_opened_cb = cb;
}

static h2_stream *match_any(void *ctx, h2_stream *stream) {
    return stream;
}

h2_response *h2_session_pop_response(h2_session *session)
{
    assert(session);
    return h2_mplx_pop_response(session->mplx);
}

/* The session wants to send more DATA for the given stream.
 */
static ssize_t stream_data_cb(nghttp2_session *ng2s,
                              int32_t stream_id,
                              uint8_t *buf,
                              size_t length,
                              uint32_t *data_flags,
                              nghttp2_data_source *source,
                              void *puser)
{
    h2_session *session = (h2_session *)puser;
    assert(session);
    
    h2_stream *stream = h2_stream_set_get(session->streams, stream_id);
    if (!stream) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, session->c,
                      "h2_stream(%ld-%d): data requested but stream not found",
                      session->id, (int)stream_id);
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    
    assert(!h2_stream_is_suspended(stream));
    
    /* Try to pop data buckets from our queue for this stream
     * until we see EOS or the buffer is full.
     */
    ssize_t total_read = 0;
    int eos = 0;
    int done = 0;
    size_t left = length;
    while (left > 0 && !done && !eos) {
        apr_status_t status = APR_SUCCESS;
        
        h2_bucket *bucket = stream->cur_out;
        stream->cur_out = NULL;
        if (!bucket) {
            status = h2_stream_read(stream, &bucket, &eos);
        }
        
        switch (status) {
            case APR_SUCCESS: {
                /* This copies out the data and modifies the bucket to
                 * reflect the amount "moved". This is easy, as this callback
                 * runs in the connection thread alone and is the sole owner
                 * of data in this queue.
                 */
                assert(bucket);
                size_t nread = h2_bucket_move(bucket, (char*)buf, left);
                if (bucket->data_len > 0) {
                    /* we could not move all, remember it for next time
                     */
                    stream->cur_out = bucket;
                    eos = 0;
                }
                else {
                    h2_bucket_destroy(bucket);
                }
                total_read += nread;
                buf += nread;
                left -= nread;
            }
                
            case APR_EAGAIN:
                /* If there is no data available, our session will automatically
                 * suspend this stream and not ask for more data until we resume
                 * it. Remember at our h2_stream that we need to do this.
                 */
                done = 1;
                if (total_read == 0) {
                    h2_stream_set_suspended(stream, 1);
                    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, session->c,
                                  "h2_stream(%ld-%d): suspending stream",
                                  session->id, (int)stream_id);
                    return NGHTTP2_ERR_DEFERRED;
                }
                break;
                
            case APR_EOF:
                eos = 1;
                done = 1;
                break;
                
            default:
                ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                              "h2_stream(%ld-%d): reading data",
                              session->id, (int)stream_id);
                return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    }
    
    if (eos) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, session->c,
                  "h2_stream(%ld-%d): requesting %ld, "
                  "sending %ld data bytes (eos=%d)",
                  session->id, (int)stream_id, (long)length, 
                  (long)total_read, eos);
    
    return total_read;
}

/* Start submitting the response to a stream request. This is possible
 * once we have all the response headers. The response body will be
 * read by the session using the callback we supply.
 */
apr_status_t h2_session_handle_response(h2_session *session,
                                        h2_stream *stream, h2_response *head)
{
    assert(session);
    apr_status_t status = APR_SUCCESS;
    int rv = 0;
    if (head->http_status) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, session->c,
                      "h2_stream(%ld-%d): submitting response %s with %d headers",
                      session->id, stream->id, head->http_status,
                      (int)head->nvlen);
        assert(head->nvlen);
        
        nghttp2_data_provider provider = {
            stream->id, stream_data_cb
        };
        rv = nghttp2_submit_response(session->ngh2, stream->id,
                                     &head->nv, head->nvlen, &provider);
    }
    else {
        rv = nghttp2_submit_rst_stream(session->ngh2, 0,
                                       stream->id, NGHTTP2_ERR_INVALID_STATE);
    }
    
    if (nghttp2_is_fatal(rv)) {
        status = APR_EGENERAL;
        h2_session_abort_int(session, rv);
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                      "submit_response: %s",
                      nghttp2_strerror(rv));
    }
    return status;
}

int h2_session_is_done(h2_session *session)
{
    assert(session);
    return (session->aborted
            || !session->ngh2
            || (!nghttp2_session_want_read(session->ngh2)
                && !nghttp2_session_want_write(session->ngh2)));
}

static int log_stream(void *ctx, h2_stream *stream)
{
    h2_session *session = (h2_session *)ctx;
    assert(session);
    ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, session->c,
                  "h2_stream(%ld-%d): in set, suspended=%d, aborted=%d, "
                  "has_data=%d",
                  session->id, stream->id, stream->suspended, stream->aborted,
                  h2_mplx_out_has_data_for(session->mplx, stream->id));
    return 1;
}

void h2_session_log_stats(h2_session *session)
{
    assert(session);
    ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, session->c,
                  "h2_session(%ld): %ld open streams",
                  session->id, h2_stream_set_size(session->streams));
    h2_stream_set_iter(session->streams, log_stream, session);
}

