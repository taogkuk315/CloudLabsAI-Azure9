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
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_config.h"
#include "h2_bucket.h"
#include "h2_bucket_queue.h"
#include "h2_resp_head.h"
#include "h2_stream.h"
#include "h2_stream_set.h"
#include "h2_response.h"
#include "h2_task.h"
#include "h2_bucket.h"
#include "h2_frame.h"
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
    apr_status_t status = h2_io_write(&session->io, (const char*)data,
                                      length, &written);
    if (status == APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, session->c,
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
                      "h2_session:  stream(%d-%d): on_data_chunk for unknown stream",
                      session->id, (int)stream_id);
        return NGHTTP2_ERR_INVALID_STREAM_ID;
    }
    if (stream->state != H2_STREAM_ST_OPEN || !stream->eoh) {
        return NGHTTP2_ERR_INVALID_STREAM_STATE;
    }
    
    apr_status_t status = h2_stream_add_data(stream, (const char *)data, len);
    return (status == APR_SUCCESS)? 0 : NGHTTP2_ERR_INVALID_STREAM_STATE;
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
    apr_status_t status = h2_io_flush(&session->io);
    
    if (session->loglvl >= APLOG_DEBUG) {
        char buffer[256];
        h2_util_frame_print(frame, buffer, sizeof(buffer)/sizeof(buffer[0]));
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, session->c,
                      "h2_session: callback on_frame_send %s", buffer);
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

static int on_stream_close_cb(nghttp2_session *ngh2, int32_t stream_id,
                              uint32_t error_code, void *userp)
{
    h2_session *session = (h2_session *)userp;
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    h2_stream *stream = h2_stream_set_get(session->streams, stream_id);
    if (stream) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                      "h2_stream(%d-%d): closing",
                      session->id, (int)stream_id);
        h2_stream_set_remove(session->streams, stream);
        h2_stream_set_remove(session->readies, stream);
        h2_stream_destroy(stream);
    }
    
    if (error_code) {
        ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, session->c,
                      "h2_stream(%d-%d): close error %d",
                      session->id, (int)stream_id, error_code);
    }
    
    return 0;
}

static int on_begin_headers_cb(nghttp2_session *ngh2,
                               const nghttp2_frame *frame, void *userp)
{
    /* This starts a new stream. */
    h2_session *session = (h2_session *)userp;
    if (session->aborted) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    h2_stream * stream = NULL;
    
    apr_status_t status = h2_stream_create(&stream, frame->hd.stream_id,
                                           session);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, session->c,
                      "h2_session: stream(%d-%d): unable to create",
                      session->id, (int)frame->hd.stream_id);
        return NGHTTP2_ERR_INVALID_STREAM_ID;
    }
    
    status = h2_stream_set_add(session->streams, stream);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, session->c,
                      "h2_session: stream(%d-%d): unable to add to pool",
                      session->id, stream->id);
        return NGHTTP2_ERR_INVALID_STREAM_ID;
    }
    
    if (stream->state != H2_STREAM_ST_IDLE) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                      "h2_session: stream(%d-%d): unexpected stream state %d",
                      session->id, (int)frame->hd.stream_id, stream->state);
        return NGHTTP2_ERR_INVALID_STREAM_STATE;
    }
    stream->state = H2_STREAM_ST_OPEN;
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, session->c,
                  "h2_session: stream(%d-%d): opened",
                  session->id, (int)frame->hd.stream_id);
    
    return 0;
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
                      "h2_session:  stream(%d-%d): on_header for unknown stream",
                      session->id, (int)frame->hd.stream_id);
        return NGHTTP2_ERR_INVALID_STREAM_ID;
    }
    
    apr_status_t status = h2_stream_add_header(stream,
                                               (const char *)name, namelen,
                                               (const char *)value, valuelen);
    return (status == APR_SUCCESS)? 0 : NGHTTP2_ERR_INVALID_STREAM_STATE;
}

static void signal_has_data(h2_session *session) {
    apr_status_t status = apr_thread_mutex_lock(session->lock);
    if (status != APR_SUCCESS) {
        ap_log_cerror( APLOG_MARK, APLOG_DEBUG, status, session->c,
                      "unable to obtain write lock");
        return;
    }
    
    status = apr_thread_cond_signal(session->has_data);
    if (status != APR_SUCCESS) {
        ap_log_cerror( APLOG_MARK, APLOG_DEBUG, status, session->c,
                      "error signalling has_data");
    }
    
    apr_thread_mutex_unlock(session->lock);
}

static void task_event_callback(h2_task *task, h2_task_event_t event, void *ctx)
{
    h2_session *session = (h2_session *)ctx;
    if (session->aborted) {
        return;
    }
    switch (event) {
        case H2_TASK_EV_READY: {
            apr_status_t status = apr_thread_mutex_lock(session->lock);
            if (status == APR_SUCCESS) {
                h2_stream *stream = h2_stream_set_get(session->streams, task->stream_id);
                if (stream) {
                    stream->resp_head = h2_task_get_resp_head(task);
                    h2_stream_set_add(session->readies, stream);
                    ap_log_cerror( APLOG_MARK, APLOG_TRACE1, 0, session->c,
                                  "stream(%d-%d): added to readies set",
                                  session->id, stream->id);
                }
                status = apr_thread_cond_signal(session->has_data);
                apr_thread_mutex_unlock(session->lock);
            }
            break;
        }
        default:
            break;
    }
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
    switch (frame->hd.type) {
        case NGHTTP2_HEADERS:
        case NGHTTP2_CONTINUATION: {
            h2_stream * stream = h2_stream_set_get(session->streams,
                                                   frame->hd.stream_id);
            if (stream == NULL) {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, session->c,
                              "h2_session:  stream(%d-%d): frame type %d "
                              "for unknown stream", session->id,
                              (int)frame->hd.stream_id, frame->hd.type);
                return NGHTTP2_ERR_INVALID_STREAM_ID;
            }
            
            if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
                h2_stream_end_headers(stream);
                
                /* Now would be a good time to actually schedule this
                 * stream for processing in a worker thread */
                h2_task *task = h2_task_create(session->id,
                                               stream->id,
                                               stream->session->c,
                                               session->data_in,
                                               session->data_out);
                h2_task_set_event_cb(task, task_event_callback, session);
                
                if (session->on_new_task_cb) {
                    session->on_new_task_cb(session, stream->id, task);
                }
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
            status = h2_stream_close_input(stream);
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, session->c,
                          "h2_stream(%d-%d): input closed",
                          session->id, (int)frame->hd.stream_id);
        }
    }
    
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                      "h2_session: stream(%d-%d): error handling frame",
                      session->id, (int)frame->hd.stream_id);
        return NGHTTP2_ERR_INVALID_STREAM_STATE;
    }
    
    return 0;
}

static void on_data_out_cb(h2_bucket_queue *q,
                           h2_bucket_queue_event_t etype,
                           h2_bucket *bucket,
                           int stream_id, int is_only_one,
                           void *ev_ctx)
{
    h2_session *session = (h2_session*)ev_ctx;
    if (session->aborted) {
        return;
    }
    switch (etype) {
        case H2_BQ_EV_BEFORE_APPEND: {
            if (is_only_one) {
                /* data arrived for a stream. Signal, so that the main thread
                 * may resume the stream if it is waiting for data. */
                signal_has_data(session);
            }
            break;
        }
        default:
            break;
    }
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

h2_session *h2_session_create(conn_rec *c, h2_config *config)
{
    nghttp2_session_callbacks *callbacks = NULL;
    nghttp2_option *options = NULL;
    
    h2_session *session = apr_pcalloc(c->pool, sizeof(h2_session));
    if (session) {
        session->id = c->id;
        session->c = c;
        session->ngh2 = NULL;
        
        session->streams = h2_stream_set_create(c->pool);
        session->readies = h2_stream_set_create(c->pool);
        
        session->data_in = h2_bucket_queue_create(c->pool, 0);
        session->data_out = h2_bucket_queue_create(c->pool, 1000);
        
        h2_io_init(&session->io, c);
        
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
        
        /* Our server nghttp2 options.
         */
        nghttp2_option_set_recv_client_preface(options, 1);
        nghttp2_option_set_peer_max_concurrent_streams(options,
                                                       config->h2_max_streams);
        
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
        
        status = apr_thread_mutex_create(&session->lock,
                                         APR_THREAD_MUTEX_DEFAULT,
                                         session->c->pool);
        if (APR_SUCCESS != status) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, status, c,
                          "unable to create mutex lock");
            h2_session_destroy(session);
            return NULL;
        }
        status = apr_thread_cond_create(&session->has_data,
                                        session->c->pool);
        if (APR_SUCCESS != status) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, status, c,
                          "unable to create cond var has_data");
            h2_session_destroy(session);
            return NULL;
        }
        
        h2_bucket_queue_set_event_cb(session->data_out,
                                     on_data_out_cb, session);
    }
    return session;
}

/* Perform shutdown operations, internal cleanup, before session is destroyed.
 */
static void h2_session_shutdown(h2_session *session)
{
    /* Abort all activities belonging to this session
     * - destroy all tasks not yet scheduled
     * - abort all tasks already running
     * - destroy all streams
     */
    apr_status_t status = apr_thread_mutex_lock(session->lock);
    if (status == APR_SUCCESS) {
        h2_stream_set_remove_all(session->readies);
        h2_stream_set_destroy_all(session->streams);
        
        apr_thread_mutex_unlock(session->lock);
    }
}


void h2_session_destroy(h2_session *session)
{
    if (!session->aborted) {
        h2_session_shutdown(session);
    }
    if (session->readies) {
        h2_stream_set_destroy(session->readies);
        session->readies = NULL;
    }
    if (session->streams) {
        h2_stream_set_destroy(session->streams);
        session->streams = NULL;
    }
    if (session->ngh2) {
        nghttp2_session_del(session->ngh2);
        session->ngh2 = NULL;
    }
    if (session->data_in) {
        h2_bucket_queue_destroy(session->data_in);
        session->data_in = NULL;
    }
    if (session->data_out) {
        h2_bucket_queue_destroy(session->data_out);
        session->data_out = NULL;
    }
    if (session->lock) {
        apr_thread_mutex_destroy(session->lock);
        session->lock = NULL;
    }
    if (session->has_data) {
        apr_thread_cond_destroy(session->has_data);
        session->has_data = NULL;
    }
    h2_io_destroy(&session->io);
}

apr_status_t h2_session_goaway(h2_session *session, apr_status_t reason)
{
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
                      "session(%d): submit goaway: %s",
                      session->id, nghttp2_strerror(rv));
    }
    return status;
}

apr_status_t h2_session_abort(h2_session *session, apr_status_t reason)
{
    if (!session->aborted) {
        session->aborted = 1;
        int err = NGHTTP2_ERR_FATAL;
        switch (reason) {
            case APR_ENOMEM:
                err = NGHTTP2_ERR_NOMEM;
            case APR_EOF:
            case APR_ECONNABORTED:
                err = NGHTTP2_ERR_EOF;
            default:
                break;
        }
        nghttp2_session_terminate_session(session->ngh2, err);
    }
    return APR_SUCCESS;
}

apr_status_t h2_session_start(h2_session *session)
{
    /* Start the conversation by submitting our SETTINGS frame */
    apr_status_t status = APR_SUCCESS;
    h2_config *config = h2_config_get(session->c);
    
    nghttp2_settings_entry settings[] = {
        { NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, config->h2_max_hl_size },
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, config->h2_window_size },
    };
    int rv = nghttp2_submit_settings(session->ngh2, NGHTTP2_FLAG_NONE,
                                     settings,
                                     sizeof(settings)/sizeof(settings[0]));
    if (rv != 0) {
        status = APR_EGENERAL;
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                      "nghttp2_submit_settings: %s", nghttp2_strerror(rv));
    }
    return status;
}

static int h2_session_want_write(h2_session *session)
{
    return nghttp2_session_want_write(session->ngh2);
}

static int resume_stream_iter(void *ctx, int stream_id,
                              h2_bucket *bucket, int index)
{
    h2_session *session = (h2_session *)ctx;
    h2_stream *stream = h2_stream_set_get(session->streams, stream_id);
    if (stream && h2_stream_is_deferred(stream)) {
        int rv = nghttp2_session_resume_data(session->ngh2, stream_id);
        h2_stream_set_deferred(stream, 0);
        if (rv) {
            ap_log_cerror(APLOG_MARK, nghttp2_is_fatal(rv)?
                          APLOG_ERR : APLOG_INFO, 0, session->c,
                          "nghttp2: resuming stream %s",
                          nghttp2_strerror(rv));
        }
    }
    return 1;
}

static void h2_session_resume_streams_with_data(h2_session *session) {
    /* Resume all streams where we have data in the out queue and 
     * which had been suspended before. */
    h2_bucket_queue_iter(session->data_out, resume_stream_iter, session);
}

apr_status_t h2_session_write(h2_session *session, apr_interval_time_t timeout)
{
    apr_status_t status = APR_SUCCESS;
    h2_session_resume_streams_with_data(session);

    if (timeout > 0 && !h2_session_want_write(session)) {
        status = apr_thread_mutex_lock(session->lock);
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_timedwait(session->has_data,
                                               session->lock,
                                               timeout);
            apr_thread_mutex_unlock(session->lock);
        }
    }
    
    if (h2_session_want_write(session)) {
        int rv = nghttp2_session_send(session->ngh2);
        if (rv != 0) {
            ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, session->c,
                          "h2_session: send error %d", rv);
            if (nghttp2_is_fatal(rv)) {
                status = APR_EGENERAL;
            }
        }
    }
    
    return status;
}

h2_stream *h2_session_get_stream(h2_session *session, int stream_id)
{
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
    return h2_io_read(&session->io, block, session_receive, session);
}

void h2_session_set_new_task_cb(h2_session *session, on_new_task *callback)
{
    session->on_new_task_cb = callback;
}

static h2_stream *match_any(void *ctx, h2_stream *stream) {
    return stream;
}

h2_stream *h2_session_pop_ready_response(h2_session *session)
{
    h2_stream *stream = h2_stream_set_find(session->readies,
                                           match_any, session);
    if (stream) {
        h2_stream *s = h2_stream_set_remove(session->readies, stream);
        ap_log_cerror( APLOG_MARK, APLOG_TRACE1, 0, session->c,
                      "stream(%d-%d): pop from readies, removed=%lx",
                      session->id, stream->id, s? (long)s : -1L);
    }
    return stream;
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
    h2_stream *stream = h2_stream_set_get(session->streams, stream_id);
    if (!stream) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, session->c,
                      "h2_stream(%d-%d): data requested but stream not found",
                      session->id, (int)stream_id);
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    
    /* Try to pop a data bucket from our queue for this stream.
     *
     * TODO: We could try to pop several buckets to fill the provided
     * buffer to the max.
     */
    h2_bucket *bucket = NULL;
    apr_status_t status = h2_bucket_queue_pop(session->data_out,
                                              APR_NONBLOCK_READ,
                                              stream_id, &bucket);
    switch (status) {
        case APR_SUCCESS: {
            /* This copies out the data and modifies the bucket to
             * reflect the amount "moved". This is easy, as this callback
             * runs in the connection thread alone and is the sole owner
             * of data in this queue.
             */
            size_t nread = h2_bucket_move(bucket, (char*)buf, length);
            if (bucket->data_len > 0) {
                /* we could not move all, put it back to the head of the queue.
                 */
                h2_bucket_queue_push(session->data_out, bucket, stream_id);
            }
            else {
                h2_bucket_destroy(bucket);
            }
            return nread;
        }
            
        case APR_EAGAIN:
            /* If there is no data available, our session will automatically
             * suspend this stream and not ask for more data until we resume
             * it. Remember at our h2_stream that we need to do this.
             */
            h2_stream_set_deferred(stream, 1);
            ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, session->c,
                          "h2_stream(%d-%d): defer data read",
                          session->id, (int)stream_id);
            return NGHTTP2_ERR_DEFERRED;
        case APR_EOF:
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
            return 0;
        default:
            return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
}

/* Start submitting the response to a stream request. This is possible
 * once we have all the response headers. The response body will be
 * read by the session using the callback we supply.
 */
apr_status_t h2_session_submit_response(h2_session *session, h2_stream *stream)
{
    apr_status_t status = APR_SUCCESS;
    int rv = 0;
    if (!stream->resp_head) {
        /* already gone? */
    }
    else if (stream->response_started) {
        /* already on its way */
        ap_log_cerror( APLOG_MARK, APLOG_DEBUG, 0, session->c,
                      "h2_stream(%d-%d): response already started",
                      session->id, stream->id);
    }
    else {
        ap_log_cerror( APLOG_MARK, APLOG_TRACE1, 0, session->c,
                      "h2_stream(%d-%d): submitting response %s with %d headers",
                      session->id, stream->id, stream->resp_head->status,
                      (int)stream->resp_head->nvlen);
        assert(stream->resp_head->nvlen);
        stream->response_started = 1;
        
        nghttp2_data_provider provider = {
            stream->id, stream_data_cb
        };
        rv = nghttp2_submit_response(session->ngh2,
                                     stream->id,
                                     &stream->resp_head->nv,
                                     stream->resp_head->nvlen,
                                     &provider);
        if (nghttp2_is_fatal(rv)) {
            status = APR_EGENERAL;
            ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                          "submit_response: %s",
                          nghttp2_strerror(rv));
        }
    }
    return status;
}

int h2_session_is_done(h2_session *session)
{
    return !session->ngh2 || (!nghttp2_session_want_read(session->ngh2)
                              && !nghttp2_session_want_write(session->ngh2));
}

