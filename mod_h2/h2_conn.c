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
#include "h2_config.h"
#include "h2_bucket_queue.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_stream_set.h"
#include "h2_task.h"
#include "h2_workers.h"
#include "h2_conn.h"

static struct h2_workers *workers;

static apr_status_t h2_session_process(h2_session *session);
static void after_stream_opened_cb(h2_session *session,
                                h2_stream *stream, h2_task *task);
static void before_stream_close_cb(h2_session *session,
                                h2_stream *stream, h2_task *task);

apr_status_t h2_conn_child_init(apr_pool_t *pool, server_rec *s)
{
    h2_config *config = h2_config_sget(s);
    apr_status_t status = APR_SUCCESS;
    int minw = h2_config_geti(config, H2_CONF_MIN_WORKERS);
    int maxw = h2_config_geti(config, H2_CONF_MAX_WORKERS);
    
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s,
                 "h2_conn: child init with conf[%s]: "
                 "min_workers=%d, max_workers=%d",
                 config->name, config->min_workers, config->max_workers);
    workers = h2_workers_create(s, pool, minw, maxw);
    return status;
}

apr_status_t h2_conn_rprocess(request_rec *r)
{
    h2_config *config = h2_config_rget(r);
    
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "h2_conn_process start");
    if (!workers) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "workers not initialized");
        return APR_EGENERAL;
    }
    
    h2_session *session = h2_session_rcreate(r, config);
    if (!session) {
        return APR_EGENERAL;
    }
    
    return h2_session_process(session);
}

apr_status_t h2_conn_process(conn_rec *c)
{
    h2_config *config = h2_config_get(c);
    
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c, "h2_conn_process start");
    if (!workers) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, c, "workers not initialized");
        return APR_EGENERAL;
    }
    
    h2_session *session = h2_session_create(c, config);
    if (!session) {
        return APR_EGENERAL;
    }
    
    return h2_session_process(session);
}

apr_status_t h2_session_process(h2_session *session)
{
    apr_status_t status = APR_SUCCESS;
    /* Start talking to the client. Apart from protocol meta data,
     * we mainly will see new http/2 streams opened by the client, which
     * basically are http requests we need to dispatch.
     *
     * There will be bursts of new streams, to be served concurrently,
     * followed by long pauses of no activity.
     *
     * Since the purpose of http/2 is to allow siumultaneous streams, we
     * need to dispatch the handling of each stream into a separate worker
     * thread, keeping this thread open for sending responses back as
     * soon as they arrive.
     * At the same time, we need to continue reading new frames from
     * our client, which may be meta (WINDOWS_UPDATEs, PING, SETTINGS) or
     * new streams.
     *
     * As long as we have streams open in this session, we cannot really rest
     * since there are two conditions to wait on: 1. new data from the client,
     * 2. new data from the open streams to send back.
     *
     * Only when we have no more streams open, can we do a blocking read
     * on our connection.
     *
     * TODO: implement graceful GO_AWAY after configurable idle time
     */
    
    /* remove the standard timeout handling */
    /* TODO: install our own? */
    ap_remove_input_filter_byhandle(session->c->input_filters, "reqtimeout");

    h2_session_set_stream_open_cb(session, after_stream_opened_cb);
    h2_session_set_stream_close_cb(session, before_stream_close_cb);
    
    status = h2_session_start(session);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                      "h2_session(%ld): startup", session->id);
        h2_session_destroy(session);
        return status;
    }
    
    apr_interval_time_t wait_micros = 0;
    static const int MAX_WAIT_MICROS = 100 * 1000; /* 100 ms */
    
    apr_time_t last_dump = apr_time_now();
    while (!h2_session_is_done(session)) {
        int have_written = 0;
        
        if (apr_time_sec(apr_time_now()) - apr_time_sec(last_dump) > 5) {
            if (workers) {
                h2_workers_log_stats(workers);
            }
            h2_session_log_stats(session);
            last_dump = apr_time_now();
        }
        
        status = h2_session_write(session, wait_micros);
        if (status == APR_SUCCESS) {
            have_written = 1;
            wait_micros = 0;
        }
        else if (status == APR_TIMEUP) {
            wait_micros *= 2;
            if (wait_micros > MAX_WAIT_MICROS) {
                wait_micros = MAX_WAIT_MICROS;
            }
            ap_log_cerror( APLOG_MARK, APLOG_DEBUG, status, session->c,
                          "timeout waiting %f ms", wait_micros/1000.0);
        }
        else {
            ap_log_cerror( APLOG_MARK, APLOG_WARNING, status, session->c,
                          "h2_session(%ld): error writing, terminating",
                          session->id);
            h2_session_abort(session, status);
            break;
        }
        
        int got_streams = !h2_stream_set_is_empty(session->streams);
        status = h2_session_read(session, got_streams?
                                 APR_NONBLOCK_READ : APR_BLOCK_READ);
        switch (status) {
            case APR_SUCCESS:
                /* successful read, reset our idle timers */
                wait_micros = 0;
                break;
            case APR_EAGAIN:
                if (!have_written) {
                    /* Nothing to read or write, we may have sessions, but
                     * the have no data yet ready to be delivered. Slowly
                     * back off to give others a chance to do their work.
                     */
                    if (wait_micros == 0) {
                        wait_micros = 100;
                    }
                }
                break;
            case APR_EOF:
            case APR_ECONNABORTED:
                ap_log_cerror( APLOG_MARK, APLOG_INFO, status, session->c,
                              "h2_session(%ld): eof on input, terminating",
                              session->id);
                h2_session_abort(session, status);
                break;
            default:
                ap_log_cerror( APLOG_MARK, APLOG_WARNING, status, session->c,
                              "h2_session(%ld): error reading, terminating",
                              session->id);
                h2_session_abort(session, status);
                break;
        }
    }
    
    ap_log_cerror( APLOG_MARK, APLOG_INFO, status, session->c,
                  "h2_session(%ld): done", session->id);
    
    if (workers) {
        h2_workers_shutdown(workers, session->id);
    }
    h2_session_destroy(session);
    
    return DONE;
}

static void after_stream_opened_cb(h2_session *session,
                                h2_stream *stream, h2_task *task)
{
    apr_status_t status = h2_workers_schedule(workers, task);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, session->c,
                      "scheduling task(%ld-%d)",
                      session->id, stream->id);
    }
}

static void before_stream_close_cb(h2_session *session,
                                h2_stream *stream, h2_task *task)
{
    if (task) {
        h2_workers_join(workers, task);
    }
}

