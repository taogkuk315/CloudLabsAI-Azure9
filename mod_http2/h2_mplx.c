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
#include <stdlib.h>

#include <apr_atomic.h>
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>
#include <apr_strings.h>
#include <apr_time.h>

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>

#include <mpm_common.h>

#include "mod_http2.h"

#include "h2.h"
#include "h2_private.h"
#include "h2_bucket_beam.h"
#include "h2_config.h"
#include "h2_c1.h"
#include "h2_conn_ctx.h"
#include "h2_protocol.h"
#include "h2_mplx.h"
#include "h2_request.h"
#include "h2_stream.h"
#include "h2_session.h"
#include "h2_c2.h"
#include "h2_workers.h"
#include "h2_util.h"


/* utility for iterating over ihash stream sets */
typedef struct {
    h2_mplx *m;
    h2_stream *stream;
    apr_time_t now;
    apr_size_t count;
} stream_iter_ctx;

/**
 * Naming convention for static functions:
 * - m_*: function only called from the master connection
 * - s_*: function only called from a secondary connection
 * - t_*: function only called from a h2_task holder
 * - mst_*: function called from everyone
 */

static apr_status_t s_mplx_be_happy(h2_mplx *m, conn_rec *c, h2_conn_ctx_t *conn_ctx);
static apr_status_t m_be_annoyed(h2_mplx *m);

static apr_status_t mplx_pollset_create(h2_mplx *m);
static apr_status_t mplx_pollset_add(h2_mplx *m, h2_stream *stream);
static apr_status_t mplx_pollset_remove(h2_mplx *m, h2_stream *stream);
static apr_status_t mplx_pollset_poll(h2_mplx *m, apr_interval_time_t timeout,
                            stream_ev_callback *on_stream_input,
                            stream_ev_callback *on_stream_output,
                            void *on_ctx);


apr_status_t h2_mplx_c1_child_init(apr_pool_t *pool, server_rec *s)
{
    return APR_SUCCESS;
}

#define H2_MPLX_ENTER(m)    \
    do { apr_status_t rv_lock; if ((rv_lock = apr_thread_mutex_lock(m->lock)) != APR_SUCCESS) {\
        return rv_lock;\
    } } while(0)

#define H2_MPLX_LEAVE(m)    \
    apr_thread_mutex_unlock(m->lock)
 
#define H2_MPLX_ENTER_ALWAYS(m)    \
    apr_thread_mutex_lock(m->lock)

#define H2_MPLX_ENTER_MAYBE(m, dolock)    \
    if (dolock) apr_thread_mutex_lock(m->lock)

#define H2_MPLX_LEAVE_MAYBE(m, dolock)    \
    if (dolock) apr_thread_mutex_unlock(m->lock)

static void m_stream_input_consumed(void *ctx, h2_bucket_beam *beam, apr_off_t length)
{
    h2_stream_in_consumed(ctx, length);
}

static int stream_is_running(h2_stream *stream)
{
    h2_conn_ctx_t *conn_ctx = h2_conn_ctx_get(stream->connection);
    return conn_ctx && conn_ctx->started_at != 0 && !conn_ctx->done;
}

static void ms_stream_joined(h2_mplx *m, h2_stream *stream)
{
    ap_assert(!stream_is_running(stream));
    
    h2_ihash_remove(m->shold, stream->id);
    h2_ihash_add(m->spurge, stream);
}

static void m_stream_cleanup(h2_mplx *m, h2_stream *stream)
{
    ap_assert(stream->state == H2_SS_CLEANUP);

    mplx_pollset_remove(m, stream);
    h2_stream_cleanup(stream);
    h2_ihash_remove(m->streams, stream->id);
    h2_iq_remove(m->q, stream->id);

    if (!stream_is_running(stream)) {
        ms_stream_joined(m, stream);
    }
    else {
        h2_ihash_add(m->shold, stream);
        if (stream->connection) {
            stream->connection->aborted = 1;
        }
    }
}

/**
 * A h2_mplx needs to be thread-safe *and* if will be called by
 * the h2_session thread *and* the h2_worker threads. Therefore:
 * - calls are protected by a mutex lock, m->lock
 * - the pool needs its own allocator, since apr_allocator_t are 
 *   not re-entrant. The separate allocator works without a 
 *   separate lock since we already protect h2_mplx itself.
 *   Since HTTP/2 connections can be expected to live longer than
 *   their HTTP/1 cousins, the separate allocator seems to work better
 *   than protecting a shared h2_session one with an own lock.
 */
h2_mplx *h2_mplx_c1_create(h2_stream *stream0, server_rec *s, apr_pool_t *parent,
                          h2_workers *workers)
{
    apr_status_t status = APR_SUCCESS;
    apr_allocator_t *allocator;
    apr_thread_mutex_t *mutex = NULL;
    h2_mplx *m = NULL;
    
    m = apr_pcalloc(parent, sizeof(h2_mplx));
    m->stream0 = stream0;
    m->c = stream0->connection;
    m->s = s;
    m->id = m->c->id;

    /* We create a pool with its own allocator to be used for
     * processing secondary connections. This is the only way to have the
     * processing independent of its parent pool in the sense that it
     * can work in another thread. Also, the new allocator needs its own
     * mutex to synchronize sub-pools.
     */
    status = apr_allocator_create(&allocator);
    if (status != APR_SUCCESS) {
        allocator = NULL;
        goto failure;
    }

    apr_allocator_max_free_set(allocator, ap_max_mem_free);
    apr_pool_create_ex(&m->pool, parent, NULL, allocator);
    if (!m->pool) goto failure;

    apr_pool_tag(m->pool, "h2_mplx");
    apr_allocator_owner_set(allocator, m->pool);

    status = apr_thread_mutex_create(&mutex, APR_THREAD_MUTEX_DEFAULT,
                                     m->pool);
    if (APR_SUCCESS != status) goto failure;
    apr_allocator_mutex_set(allocator, mutex);

    status = apr_thread_mutex_create(&m->lock, APR_THREAD_MUTEX_DEFAULT,
                                     m->pool);
    if (APR_SUCCESS != status) goto failure;

    status = apr_thread_cond_create(&m->join_wait, m->pool);
    if (APR_SUCCESS != status) goto failure;

    m->max_streams = h2_config_sgeti(s, H2_CONF_MAX_STREAMS);
    m->stream_max_mem = h2_config_sgeti(s, H2_CONF_STREAM_MAX_MEM);

    m->streams = h2_ihash_create(m->pool, offsetof(h2_stream,id));
    m->shold = h2_ihash_create(m->pool, offsetof(h2_stream,id));
    m->spurge = h2_ihash_create(m->pool, offsetof(h2_stream,id));
    m->q = h2_iq_create(m->pool, m->max_streams);

    m->workers = workers;
    m->processing_max = workers->max_workers;
    m->processing_limit = 6; /* the original h1 max parallel connections */
    m->last_mood_change = apr_time_now();
    m->mood_update_interval = apr_time_from_msec(100);

    m->spare_c2 = apr_array_make(m->pool, 10, sizeof(conn_rec*));

    status = mplx_pollset_create(m);
    if (APR_SUCCESS != status) goto failure;

    return m;

failure:
    if (m->pool) {
        apr_pool_destroy(m->pool);
    }
    else if (allocator) {
        apr_allocator_destroy(allocator);
    }
    return NULL;
}

int h2_mplx_c1_shutdown(h2_mplx *m)
{
    int max_stream_id_started = 0;
    
    H2_MPLX_ENTER(m);

    max_stream_id_started = m->max_stream_id_started;
    /* Clear schedule queue, disabling existing streams from starting */ 
    h2_iq_clear(m->q);

    H2_MPLX_LEAVE(m);
    return max_stream_id_started;
}

static int s_output_consumed_signal(h2_mplx *m, h2_conn_ctx_t *conn_ctx)
{
    if (conn_ctx->beam_out) {
        return h2_beam_report_consumption(conn_ctx->beam_out);
    }
    return 0;
}

static int m_stream_purge_iter(void *ctx, void *val)
{   
    h2_mplx *m = ctx;
    h2_conn_ctx_t *conn_ctx = NULL;
    h2_stream *stream = val;

    h2_ihash_remove(m->spurge, stream->id);
    ap_assert(stream->state == H2_SS_CLEANUP);
    
    if (stream->input) {
        h2_beam_destroy(stream->input, m->c);
        stream->input = NULL;
    }

    if (stream->connection) {
        conn_rec *secondary;
        int reuse_c2 = 0;
        
        secondary = stream->connection;
        stream->connection = NULL;

        conn_ctx = h2_conn_ctx_get(secondary);
        h2_conn_ctx_detach(secondary);
        if (conn_ctx && (m->s->keep_alive_max == 0
                         || secondary->keepalives < m->s->keep_alive_max)) {
            reuse_c2 = ((m->spare_c2->nelts < (m->processing_limit * 3 / 2))
                               && !secondary->aborted);
        }

        if (reuse_c2) {
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, m->c, APLOGNO(03385)
                          "h2_c2(%s), reuse secondary", conn_ctx->id);
            h2_conn_ctx_destroy(conn_ctx);
            APR_ARRAY_PUSH(m->spare_c2, conn_rec*) = secondary;
        }
        else {
            h2_c2_destroy(secondary);
        }
    }

    if (stream->mplx_pipe_pool) {
        apr_pool_destroy(stream->mplx_pipe_pool);
    }

    h2_stream_destroy(stream);
    return 0;
}

typedef struct {
    h2_mplx_stream_cb *cb;
    void *ctx;
} stream_iter_ctx_t;

static int m_stream_iter_wrap(void *ctx, void *stream)
{
    stream_iter_ctx_t *x = ctx;
    return x->cb(stream, x->ctx);
}

apr_status_t h2_mplx_c1_streams_do(h2_mplx *m, h2_mplx_stream_cb *cb, void *ctx)
{
    stream_iter_ctx_t x;
    
    H2_MPLX_ENTER(m);

    x.cb = cb;
    x.ctx = ctx;
    h2_ihash_iter(m->streams, m_stream_iter_wrap, &x);
        
    H2_MPLX_LEAVE(m);
    return APR_SUCCESS;
}

static int m_report_stream_iter(void *ctx, void *val) {
    h2_mplx *m = ctx;
    h2_stream *stream = val;
    h2_conn_ctx_t *conn_ctx = h2_conn_ctx_get(stream->connection);
    if (APLOGctrace1(m->c)) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                      H2_STRM_MSG(stream, "started=%d, scheduled=%d, ready=%d, out_buffer=%ld"), 
                      !!stream->connection, stream->scheduled, h2_stream_is_ready(stream),
                      (long)h2_beam_get_buffered(stream->output));
    }
    if (conn_ctx) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, m->c, /* NO APLOGNO */
                      H2_STRM_MSG(stream, "->03198: %s %s %s"
                      "[started=%d/done=%d]"), 
                      conn_ctx->request->method, conn_ctx->request->authority,
                      conn_ctx->request->path, conn_ctx->started_at != 0,
                      conn_ctx->done);
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, m->c, /* NO APLOGNO */
                      H2_STRM_MSG(stream, "->03198: no task"));
    }
    return 1;
}

static int m_unexpected_stream_iter(void *ctx, void *val) {
    h2_mplx *m = ctx;
    h2_stream *stream = val;
    ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, m->c, /* NO APLOGNO */
                  H2_STRM_MSG(stream, "unexpected, started=%d, scheduled=%d, ready=%d"), 
                  !!stream->connection, stream->scheduled, h2_stream_is_ready(stream));
    return 1;
}

static int m_stream_cancel_iter(void *ctx, void *val) {
    h2_mplx *m = ctx;
    h2_stream *stream = val;

    /* disable input consumed reporting */
    if (stream->input) {
        h2_beam_abort(stream->input, m->c);
    }
    /* take over event monitoring */
    h2_stream_set_monitor(stream, NULL);
    /* Reset, should transit to CLOSED state */
    h2_stream_rst(stream, H2_ERR_NO_ERROR);
    /* All connection data has been sent, simulate cleanup */
    h2_stream_dispatch(stream, H2_SEV_EOS_SENT);
    m_stream_cleanup(m, stream);  
    return 0;
}

void h2_mplx_c1_destroy(h2_mplx *m)
{
    apr_status_t status;
    int i, wait_secs = 60, old_aborted;

    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, m->c,
                  "h2_mplx(%ld): start release", m->id);
    /* How to shut down a h2 connection:
     * 0. abort and tell the workers that no more tasks will come from us */
    m->aborted = 1;
    h2_workers_unregister(m->workers, m);
    
    H2_MPLX_ENTER_ALWAYS(m);

    /* While really terminating any secondary connections, treat the master
     * connection as aborted. It's not as if we could send any more data
     * at this point. */
    old_aborted = m->c->aborted;
    m->c->aborted = 1;

    /* How to shut down a h2 connection:
     * 1. cancel all streams still active */
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c, 
                  "h2_mplx(%ld): release, %d/%d/%d streams (total/hold/purge), %d active tasks", 
                  m->id, (int)h2_ihash_count(m->streams),
                  (int)h2_ihash_count(m->shold), (int)h2_ihash_count(m->spurge), m->processing_count);
    while (!h2_ihash_iter(m->streams, m_stream_cancel_iter, m)) {
        /* until empty */
    }
    
    /* 2. no more streams should be scheduled or in the active set */
    ap_assert(h2_ihash_empty(m->streams));
    ap_assert(h2_iq_empty(m->q));
    
    /* 3. while workers are busy on this connection, meaning they
     *    are processing tasks from this connection, wait on them finishing
     *    in order to wake us and let us check again. 
     *    Eventually, this has to succeed. */    
    for (i = 0; h2_ihash_count(m->shold) > 0; ++i) {
        status = apr_thread_cond_timedwait(m->join_wait, m->lock, apr_time_from_sec(wait_secs));
        
        if (APR_STATUS_IS_TIMEUP(status)) {
            /* This can happen if we have very long running requests
             * that do not time out on IO. */
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, m->c, APLOGNO(03198)
                          "h2_mplx(%ld): waited %d sec for %d tasks", 
                          m->id, i*wait_secs, (int)h2_ihash_count(m->shold));
            h2_ihash_iter(m->shold, m_report_stream_iter, m);
        }
    }
    m->join_wait = NULL;

    /* 4. With all workers done, all streams should be in spurge */
    ap_assert(m->processing_count == 0);
    if (!h2_ihash_empty(m->shold)) {
        ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, m->c, APLOGNO(03516)
                      "h2_mplx(%ld): unexpected %d streams in hold", 
                      m->id, (int)h2_ihash_count(m->shold));
        h2_ihash_iter(m->shold, m_unexpected_stream_iter, m);
    }
    
    m->c->aborted = old_aborted;
    H2_MPLX_LEAVE(m);

    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c, "h2_mplx(%ld): released", m->id);
}

apr_status_t h2_mplx_c1_stream_cleanup(h2_mplx *m, h2_stream *stream)
{
    H2_MPLX_ENTER(m);
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, m->c, 
                  H2_STRM_MSG(stream, "cleanup"));
    m_stream_cleanup(m, stream);        
    
    H2_MPLX_LEAVE(m);
    return APR_SUCCESS;
}

const h2_stream *h2_mplx_c2_stream_get(h2_mplx *m, int stream_id)
{
    h2_stream *s = NULL;
    
    H2_MPLX_ENTER_ALWAYS(m);
    s = h2_ihash_get(m->streams, stream_id);
    H2_MPLX_LEAVE(m);

    return s;
}

apr_status_t h2_mplx_c2_set_stream_output(
    h2_mplx *m, int stream_id, h2_bucket_beam *output)
{
    h2_stream *s = NULL;
    apr_status_t rv = APR_EINVAL;

    H2_MPLX_ENTER_ALWAYS(m);
    s = h2_ihash_get(m->streams, stream_id);
    if (s && !m->aborted) {
        ap_assert(s->output == NULL);  /* should be called only once */
        s->output = output;
        rv = APR_SUCCESS;
    }
    H2_MPLX_LEAVE(m);

    return rv;
}

static apr_status_t s_out_close(h2_mplx *m, conn_rec *c, h2_conn_ctx_t *conn_ctx)
{
    apr_status_t status = APR_SUCCESS;
    h2_stream *stream;
    
    if (!conn_ctx) {
        return APR_ECONNABORTED;
    }

    ++c->keepalives;
    stream = h2_ihash_get(m->streams, conn_ctx->stream_id);
    if (!stream) {
        return APR_ECONNABORTED;
    }

    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, c,
                  "h2_mplx(%s): close", conn_ctx->id);
    status = h2_beam_close(conn_ctx->beam_out, c);
    s_output_consumed_signal(m, conn_ctx);
    return status;
}

apr_status_t h2_mplx_c1_poll(h2_mplx *m, apr_interval_time_t timeout,
                            stream_ev_callback *on_stream_input,
                            stream_ev_callback *on_stream_output,
                            void *on_ctx)
{
    apr_status_t rv;

    H2_MPLX_ENTER(m);

    if (m->aborted) {
        rv = APR_ECONNABORTED;
        goto cleanup;
    }
    /* Purge (destroy) streams outside of pollset processing.
     * Streams that are registered in the pollset, will be removed
     * when they are destroyed, but the pollset works on copies
     * of these registrations. So, if we destroy streams while
     * processing pollset events, we might access freed memory.
     */
    if (!h2_ihash_empty(m->spurge)) {
        while (!h2_ihash_iter(m->spurge, m_stream_purge_iter, m)) {
            /* repeat until empty */
        }
    }
    rv = mplx_pollset_poll(m, timeout, on_stream_input, on_stream_output, on_ctx);

cleanup:
    H2_MPLX_LEAVE(m);
    return rv;
}

apr_status_t h2_mplx_c1_reprioritize(h2_mplx *m, h2_stream_pri_cmp_fn *cmp,
                                    h2_session *session)
{
    apr_status_t status;
    
    H2_MPLX_ENTER(m);

    if (m->aborted) {
        status = APR_ECONNABORTED;
    }
    else {
        h2_iq_sort(m->q, cmp, session);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                      "h2_mplx(%ld): reprioritize tasks", m->id);
        status = APR_SUCCESS;
    }

    H2_MPLX_LEAVE(m);
    return status;
}

static void ms_register_if_needed(h2_mplx *m, int from_master) 
{
    if (!m->aborted && !m->is_registered && !h2_iq_empty(m->q)) {
        apr_status_t status = h2_workers_register(m->workers, m); 
        if (status == APR_SUCCESS) {
            m->is_registered = 1;
        }
        else if (from_master) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, status, m->c, APLOGNO(10021)
                          "h2_mplx(%ld): register at workers", m->id);
        }
    }
}

static apr_status_t c1_process_stream(h2_mplx *m,
                                      h2_stream *stream,
                                      h2_stream_pri_cmp_fn *cmp,
                                      h2_session *session)
{
    apr_status_t rv;

    if (m->aborted) {
        rv = APR_ECONNABORTED;
        goto cleanup;
    }
    if (!stream->request) {
        rv = APR_EINVAL;
        goto cleanup;
    }
    if (APLOGctrace1(m->c)) {
        const h2_request *r = stream->request;
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                      H2_STRM_MSG(stream, "process %s %s://%s%s chunked=%d"),
                      r->method, r->scheme, r->authority, r->path, r->chunked);
    }

    rv = h2_stream_setup_input(stream);
    if (APR_SUCCESS != rv) goto cleanup;

    stream->scheduled = 1;
    h2_ihash_add(m->streams, stream);
    if (h2_stream_is_ready(stream)) {
        /* already have a response */
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                      H2_STRM_MSG(stream, "process, ready already"));
    }
    else {
        h2_iq_add(m->q, stream->id, cmp, session);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                      H2_STRM_MSG(stream, "process, added to q"));
    }

cleanup:
    return rv;
}

apr_status_t h2_mplx_c1_process(h2_mplx *m,
                                h2_iqueue *ready_to_process,
                                h2_stream_get_fn *get_stream,
                                h2_stream_pri_cmp_fn *stream_pri_cmp,
                                h2_session *session)
{
    apr_status_t rv;
    int sid;

    H2_MPLX_ENTER(m);

    while ((sid = h2_iq_shift(ready_to_process)) > 0) {
        h2_stream *stream = get_stream(session, sid);
        if (stream) {
            ap_assert(!stream->scheduled);
            rv = c1_process_stream(session->mplx, stream, stream_pri_cmp, session);
            if (APR_SUCCESS != rv) {
                h2_stream_rst(stream, H2_ERR_INTERNAL_ERROR);
            }
        }
        else {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                          "h2_stream(%ld-%d): not found to process", m->id, sid);
        }
    }
    ms_register_if_needed(m, 1);

    H2_MPLX_LEAVE(m);
    return rv;
}

apr_status_t h2_mplx_c1_fwd_input(h2_mplx *m, struct h2_iqueue *input_pending,
                                  h2_stream_get_fn *get_stream,
                                  struct h2_session *session)
{
    int sid;

    H2_MPLX_ENTER(m);

    while ((sid = h2_iq_shift(input_pending)) > 0) {
        h2_stream *stream = get_stream(session, sid);
        if (stream) {
            h2_stream_flush_input(stream);
            if (stream->input) {
                if (stream->input_closed) {
                    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, m->c,
                                  H2_STRM_MSG(stream, "closing input beam"));
                    h2_beam_close(stream->input, m->c);
                }
                if (stream->pin_send_write) {
                    apr_file_putc(1, stream->pin_send_write);
                }
            }
        }
    }

    H2_MPLX_LEAVE(m);
    return APR_SUCCESS;
}


static conn_rec *s_next_c2(h2_mplx *m)
{
    h2_stream *stream;
    apr_status_t rv;
    int sid;

    while (!m->aborted && (m->processing_count < m->processing_limit)
           && (sid = h2_iq_shift(m->q)) > 0) {
        
        stream = h2_ihash_get(m->streams, sid);
        if (stream) {
            conn_rec *c2, **pc2;
            h2_conn_ctx_t *conn_ctx;

            pc2 = (conn_rec **)apr_array_pop(m->spare_c2);
            if (pc2) {
                c2 = *pc2;
                c2->aborted = 0;
            }
            else {
                c2 = h2_c2_create(m->c, stream->id, m->pool);
            }
            stream->connection = c2;

            if (sid > m->max_stream_id_started) {
                m->max_stream_id_started = sid;
            }

            conn_ctx = h2_conn_ctx_create_for_c2(c2, stream);
            apr_table_setn(c2->notes, H2_TASK_ID_NOTE, conn_ctx->id);

            apr_pool_create(&stream->mplx_pipe_pool, m->pool);
            apr_pool_tag(stream->mplx_pipe_pool, "H2_MPLX_PIPE");
            rv = apr_file_pipe_create_pools(&stream->pout_recv_write, &conn_ctx->put_send_write,
                                            APR_FULL_NONBLOCK,
                                            stream->mplx_pipe_pool, conn_ctx->pool);
            if (APR_SUCCESS != rv) {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, rv, m->c,
                              H2_STRM_LOG(APLOGNO(), stream,
                              "error creating output pipe"));
                /* TODO: what do do here? */
            }
            rv = apr_file_pipe_create_pools(&conn_ctx->pin_recv_write, &stream->pin_send_write,
                                            APR_READ_BLOCK,
                                            conn_ctx->pool, stream->mplx_pipe_pool);
            if (APR_SUCCESS != rv) {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, rv, m->c,
                              H2_STRM_LOG(APLOGNO(), stream,
                              "error creating input pipe"));
                /* TODO: what do do here? */
            }

            if (stream->input) {
                rv = apr_file_pipe_create_pools(&stream->pin_recv_read, &conn_ctx->pin_send_read,
                                                APR_FULL_NONBLOCK,
                                                conn_ctx->pool, stream->mplx_pipe_pool);
                if (APR_SUCCESS != rv) {
                    ap_log_cerror(APLOG_MARK, APLOG_ERR, rv, m->c,
                                  H2_STRM_LOG(APLOGNO(), stream,
                                  "error creating input read pipe"));
                    /* TODO: what do do here? */
                }

                h2_beam_on_consumed(stream->input, NULL,
                                    m_stream_input_consumed, stream);
                conn_ctx->beam_in = stream->input;
            }
            if (!conn_ctx->beam_in || h2_beam_is_closed(conn_ctx->beam_in)) {
                apr_file_close(stream->pin_send_write);
            }

            mplx_pollset_add(m, stream);
            ++m->processing_count;
            return c2;
        }
    }

    if (m->processing_count >= m->processing_limit && !h2_iq_empty(m->q)) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, m->c,
                      "h2_session(%ld): delaying request processing. "
                      "Current limit is %d and %d workers are in use.",
                      m->id, m->processing_limit, m->processing_count);
    }
    return NULL;
}

apr_status_t h2_mplx_worker_pop_c2(h2_mplx *m, conn_rec **out_c)
{
    apr_status_t rv = APR_EOF;
    
    *out_c = NULL;
    ap_assert(m);
    ap_assert(m->lock);
    
    if (APR_SUCCESS != (rv = apr_thread_mutex_lock(m->lock))) {
        return rv;
    }
    
    if (m->aborted) {
        rv = APR_EOF;
    }
    else {
        *out_c = s_next_c2(m);
        rv = (*out_c != NULL && !h2_iq_empty(m->q))? APR_EAGAIN : APR_SUCCESS;
    }
    if (APR_EAGAIN != rv) {
        m->is_registered = 0; /* h2_workers will discard this mplx */
    }
    H2_MPLX_LEAVE(m);
    return rv;
}

static void s_c2_done(h2_mplx *m, conn_rec *c, h2_conn_ctx_t *conn_ctx)
{
    h2_stream *stream;

    ap_assert(conn_ctx);
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_mplx(%s): task done", conn_ctx->id);
    s_out_close(m, c, conn_ctx);
    
    ap_assert(conn_ctx->done == 0);
    conn_ctx->done = 1;
    conn_ctx->done_at = apr_time_now();
    apr_file_close(conn_ctx->pin_recv_write);
    apr_file_close(conn_ctx->put_send_write);
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                  "h2_mplx(%s): request done, %f ms elapsed", conn_ctx->id,
                  (conn_ctx->done_at - conn_ctx->started_at) / 1000.0);
    
    if (!c->aborted && conn_ctx->started_at > m->last_mood_change) {
        s_mplx_be_happy(m, c, conn_ctx);
    }
    
    stream = h2_ihash_get(m->streams, conn_ctx->stream_id);
    if (stream) {
        /* stream not done yet. */
        /* stream not cleaned up, stay around */
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      H2_STRM_MSG(stream, "task_done, stream open"));
        if (stream->input) {
            h2_beam_abort(stream->input, c);
        }
    }
    else if ((stream = h2_ihash_get(m->shold, conn_ctx->stream_id)) != NULL) {
        /* stream is done, was just waiting for this. */
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      H2_STRM_MSG(stream, "task_done, in hold"));
        if (stream->input) {
            h2_beam_abort(stream->input, c);
        }
        ms_stream_joined(m, stream);
    }
    else if ((stream = h2_ihash_get(m->spurge, conn_ctx->stream_id)) != NULL) {
        ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, c,
                      H2_STRM_LOG(APLOGNO(03517), stream, "already in spurge"));
        ap_assert("stream should not be in spurge" == NULL);
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, c, APLOGNO(03518)
                      "h2_mplx(%s): task_done, stream not found", 
                      conn_ctx->id);
        ap_assert("stream should still be available" == NULL);
    }
}

void h2_mplx_worker_c2_done(conn_rec *c2, conn_rec **out_c2)
{
    h2_conn_ctx_t *conn_ctx = h2_conn_ctx_get(c2);
    h2_mplx *m;

    if (!conn_ctx || !conn_ctx->mplx) return;
    m = conn_ctx->mplx;

    H2_MPLX_ENTER_ALWAYS(m);

    --m->processing_count;
    s_c2_done(m, c2, conn_ctx);
    
    if (m->join_wait) {
        apr_thread_cond_signal(m->join_wait);
    }
    if (out_c2) {
        /* caller wants another connection to process */
        *out_c2 = s_next_c2(m);
    }
    ms_register_if_needed(m, 0);

    H2_MPLX_LEAVE(m);
}

/*******************************************************************************
 * h2_mplx DoS protection
 ******************************************************************************/

static apr_status_t s_mplx_be_happy(h2_mplx *m, conn_rec *c, h2_conn_ctx_t *conn_ctx)
{
    apr_time_t now;            

    --m->irritations_since;
    now = apr_time_now();
    if (m->processing_limit < m->processing_max
        && (now - m->last_mood_change >= m->mood_update_interval
            || m->irritations_since < -m->processing_limit)) {
        m->processing_limit = H2MIN(m->processing_limit * 2, m->processing_max);
        m->last_mood_change = now;
        m->irritations_since = 0;
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_mplx(%ld): mood update, increasing worker limit to %d",
                      m->id, m->processing_limit);
    }
    return APR_SUCCESS;
}

static apr_status_t m_be_annoyed(h2_mplx *m)
{
    apr_status_t status = APR_SUCCESS;
    apr_time_t now;            

    ++m->irritations_since;
    now = apr_time_now();
    if (m->processing_limit > 2 &&
        ((now - m->last_mood_change >= m->mood_update_interval)
         || (m->irritations_since >= m->processing_limit))) {
            
        if (m->processing_limit > 16) {
            m->processing_limit = 16;
        }
        else if (m->processing_limit > 8) {
            m->processing_limit = 8;
        }
        else if (m->processing_limit > 4) {
            m->processing_limit = 4;
        }
        else if (m->processing_limit > 2) {
            m->processing_limit = 2;
        }
        m->last_mood_change = now;
        m->irritations_since = 0;
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                      "h2_mplx(%ld): mood update, decreasing worker limit to %d",
                      m->id, m->processing_limit);
    }
    return status;
}

/*******************************************************************************
 * mplx master events dispatching
 ******************************************************************************/

static int reset_is_acceptable(h2_stream *stream)
{
    /* client may terminate a stream via H2 RST_STREAM message at any time.
     * This is annyoing when we have committed resources (e.g. worker threads)
     * to it, so our mood (e.g. willingness to commit resources on this
     * connection in the future) goes down.
     *
     * This is a DoS protection. We do not want to make it too easy for
     * a client to eat up server resources.
     *
     * However: there are cases where a RST_STREAM is the only way to end
     * a request. This includes websockets and server-side-event streams (SSEs).
     * The responses to such requests continue forever otherwise.
     *
     */
    if (!stream_is_running(stream)) return 1;
    if (!(stream->id & 0x01)) return 1; /* stream initiated by us. acceptable. */
    if (!stream->response) return 0; /* no response headers produced yet. bad. */
    if (!stream->out_data_frames) return 0; /* no response body data sent yet. bad. */
    return 1; /* otherwise, be forgiving */
}

apr_status_t h2_mplx_c1_client_rst(h2_mplx *m, int stream_id)
{
    h2_stream *stream;
    apr_status_t status = APR_SUCCESS;

    H2_MPLX_ENTER_ALWAYS(m);
    stream = h2_ihash_get(m->streams, stream_id);
    if (stream && !reset_is_acceptable(stream)) {
        status = m_be_annoyed(m);
    }
    H2_MPLX_LEAVE(m);
    return status;
}

static apr_status_t mplx_pollset_create(h2_mplx *m)
{
    apr_status_t rv;
    int max_pdfs;

    /* stream0 output, pdf_out+pfd_in_consume per active streams */
    max_pdfs = 1 + 2 * H2MIN(m->processing_max, m->max_streams);
    rv = apr_pollset_create(&m->pollset, max_pdfs, m->pool, APR_POLLSET_NOCOPY);
    if (APR_SUCCESS != rv) goto cleanup;

    mplx_pollset_add(m, m->stream0);

cleanup:
    return rv;
}

static apr_status_t mplx_pollset_add(h2_mplx *m, h2_stream *stream)
{
    apr_status_t rv;
    const char *name = "";

    if (!stream->pfd_out_write) {
        stream->pfd_out_write = apr_pcalloc(m->stream0->pool, sizeof(*stream->pfd_out_write));
        stream->pfd_out_write->p = stream->mplx_pipe_pool? stream->mplx_pipe_pool : stream->pool;
        stream->pfd_out_write->client_data = stream;
    }
    else if (stream->pfd_out_write->reqevents) {
        name = "removing out";
        rv = apr_pollset_remove(m->pollset, stream->pfd_out_write);
        if (APR_SUCCESS != rv) goto cleanup;
    }

    if (stream->id == 0) {
        /* primary connection */
        stream->pfd_out_write->desc_type = APR_POLL_SOCKET;
        stream->pfd_out_write->desc.s = ap_get_conn_socket(m->stream0->connection);
        apr_socket_opt_set(stream->pfd_out_write->desc.s, APR_SO_NONBLOCK, 1);
    }
    else {
        /* secondary connection */
        stream->pfd_out_write->desc_type = APR_POLL_FILE;
        stream->pfd_out_write->desc.f = stream->pout_recv_write;
    }
    stream->pfd_out_write->reqevents = APR_POLLIN | APR_POLLERR | APR_POLLHUP;
    name = "adding out";
    rv = apr_pollset_add(m->pollset, stream->pfd_out_write);
    if (APR_SUCCESS != rv) goto cleanup;

    if (stream->id && stream->pin_recv_read) {
        if (!stream->pfd_in_read) {
            stream->pfd_in_read = apr_pcalloc(m->stream0->pool, sizeof(*stream->pfd_in_read));
            stream->pfd_in_read->p = stream->mplx_pipe_pool? stream->mplx_pipe_pool : stream->pool;
            stream->pfd_in_read->client_data = stream;
        }
        else {
            name = "removing in_read";
            rv = apr_pollset_remove(m->pollset, stream->pfd_in_read);
            if (APR_SUCCESS != rv) goto cleanup;
        }
        stream->pfd_in_read->desc_type = APR_POLL_FILE;
        stream->pfd_in_read->desc.f = stream->pin_recv_read;
        stream->pfd_in_read->reqevents = APR_POLLIN | APR_POLLERR | APR_POLLHUP;
        name = "adding in_read";
        rv = apr_pollset_add(m->pollset, stream->pfd_in_read);
    }

cleanup:
    if (APR_SUCCESS != rv) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, rv, m->c,
                      H2_STRM_LOG(APLOGNO(), stream,
                      "error while adding to pollset: %s"), name);
    }
    return rv;
}

static apr_status_t mplx_pollset_remove(h2_mplx *m, h2_stream *stream)
{
    apr_status_t rv = APR_SUCCESS;
    const char *name = "";

    if (stream->pfd_out_write) {
        name = "out";
        rv = apr_pollset_remove(m->pollset, stream->pfd_out_write);
        if (APR_SUCCESS != rv) goto cleanup;
        stream->pfd_out_write->reqevents = 0;
    }
    if (stream->pfd_in_read) {
        name = "in_read";
        rv = apr_pollset_remove(m->pollset, stream->pfd_in_read);
        if (APR_SUCCESS != rv) goto cleanup;
        stream->pfd_in_read->reqevents = 0;
    }
cleanup:
    if (APR_SUCCESS != rv) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, rv, m->c,
                      H2_STRM_LOG(APLOGNO(), stream,
                      "error removing from pollset %s"), name);
    }
    return rv;
}

static apr_status_t mplx_pollset_poll(h2_mplx *m, apr_interval_time_t timeout,
                            stream_ev_callback *on_stream_input,
                            stream_ev_callback *on_stream_output,
                            void *on_ctx)
{
    apr_status_t rv;
    const apr_pollfd_t *results;
    apr_int32_t nresults, i;

    /* Make sure we are not called recursively. */
    ap_assert(!m->polling);
    m->polling = 1;
    do {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, m->c,
                      "h2_mplx(%ld): enter polling timeout=%d",
                      m->id, (int)apr_time_sec(timeout));
        H2_MPLX_LEAVE(m);
        do {
            rv = apr_pollset_poll(m->pollset, timeout >= 0? timeout : -1, &nresults, &results);
        } while (APR_STATUS_IS_EINTR(rv));
        H2_MPLX_ENTER(m);

        if (APR_SUCCESS != rv) {
            if (APR_STATUS_IS_TIMEUP(rv)) {
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, m->c,
                              "h2_mplx(%ld): polling timed out ",
                              m->id);
            }
            else {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, rv, m->c, APLOGNO()
                              "h2_mplx(%ld): polling failed", m->id);
            }
            goto cleanup;
        }

        for (i = 0; i < nresults; i++) {
            const apr_pollfd_t *pfd = &results[i];
            h2_stream *stream = pfd->client_data;
            if (stream->id == 0) {
                if (on_stream_input) {
                    H2_MPLX_LEAVE(m);
                    on_stream_input(on_ctx, stream);
                    H2_MPLX_ENTER(m);
                }
            }
            else if (stream->pfd_out_write && stream->pfd_out_write->desc.f == pfd->desc.f) {
                /* output is available */
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, m->c,
                              H2_STRM_MSG(stream, "poll output event %hx/%hx"),
                              pfd->rtnevents, stream->pfd_out_write->reqevents);
                if (stream->id) {
                    h2_util_drain_pipe(stream->pout_recv_write);
                    if (pfd->rtnevents & APR_POLLHUP) {
                        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                                      H2_STRM_MSG(stream, "output closed"));
                    }
                    else if (pfd->rtnevents & APR_POLLIN) {
                        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                                      H2_STRM_MSG(stream, "output ready"));
                    }
                    else if (pfd->rtnevents & APR_POLLERR) {
                        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, m->c,
                                      H2_STRM_MSG(stream, "output error"));
                    }
                }
                else {
                    /* event on stream0, e.g. the primary connection socket */
                }

                if (on_stream_output) {
                    H2_MPLX_LEAVE(m);
                    on_stream_output(on_ctx, stream);
                    H2_MPLX_ENTER(m);
                }
            }
            else if (stream->pfd_in_read && stream->pfd_in_read->desc.f == pfd->desc.f) {
                /* input has been consumed */
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, m->c,
                              H2_STRM_MSG(stream, "poll input event %hx/%hx"),
                              pfd->rtnevents, stream->pfd_in_read->reqevents);
                h2_util_drain_pipe(stream->pin_recv_read);
                if (on_stream_input) {
                    H2_MPLX_LEAVE(m);
                    on_stream_input(on_ctx, stream);
                    H2_MPLX_ENTER(m);
                }
            }
        }
        break;
    } while(1);

cleanup:
    m->polling = 0;
    return rv;
}
