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
#include <apr_strings.h>

#include <ap_mpm.h>
#include <ap_mmn.h>

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>
#include <http_connection.h>
#include <http_protocol.h>
#include <http_request.h>

#include <mpm_common.h>

#include "h2_private.h"
#include "h2.h"
#include "h2_config.h"
#include "h2_conn_ctx.h"
#include "h2_c1_status.h"
#include "h2_mplx.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_h2.h"
#include "h2_c2.h"
#include "h2_workers.h"
#include "h2_c1.h"
#include "h2_version.h"

static struct h2_workers *workers;

static int async_mpm;

apr_status_t h2_c1_child_init(apr_pool_t *pool, server_rec *s)
{
    apr_status_t status = APR_SUCCESS;
    int minw, maxw;
    int max_threads_per_child = 0;
    int idle_secs = 0;

    ap_mpm_query(AP_MPMQ_MAX_THREADS, &max_threads_per_child);
    
    status = ap_mpm_query(AP_MPMQ_IS_ASYNC, &async_mpm);
    if (status != APR_SUCCESS) {
        /* some MPMs do not implemnent this */
        async_mpm = 0;
        status = APR_SUCCESS;
    }

    h2_config_init(pool);

    h2_get_num_workers(s, &minw, &maxw);
    idle_secs = h2_config_sgeti(s, H2_CONF_MAX_WORKER_IDLE_SECS);
    ap_log_error(APLOG_MARK, APLOG_TRACE3, 0, s,
                 "h2_workers: min=%d max=%d, mthrpchild=%d, idle_secs=%d", 
                 minw, maxw, max_threads_per_child, idle_secs);
    workers = h2_workers_create(s, pool, minw, maxw, idle_secs);
 
    ap_register_input_filter("H2_IN", h2_c1_filter_input,
                             NULL, AP_FTYPE_CONNECTION);
   
    return h2_mplx_m_child_init(pool, s);
}

apr_status_t h2_c1_setup(conn_rec *c, request_rec *r, server_rec *s)
{
    h2_session *session;
    h2_conn_ctx_t *ctx;
    apr_status_t rv;
    
    if (!workers) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, c, APLOGNO(02911) 
                      "workers not initialized");
        rv = APR_EGENERAL;
        goto cleanup;
    }

    rv = h2_session_create(&session, c, r, s, workers);
    if (APR_SUCCESS != rv) goto cleanup;

    ctx = h2_conn_ctx_get(c);
    ap_assert(ctx);
    ctx->session = session;
    /* remove the input filter of mod_reqtimeout, now that the connection
     * is established and we have swtiched to h2. reqtimeout has supervised
     * possibly configured handshake timeouts and needs to get out of the way
     * now since the rest of its state handling assumes http/1.x to take place. */
    ap_remove_input_filter_byhandle(c->input_filters, "reqtimeout");

cleanup:
    return rv;
}

apr_status_t h2_c1_run(conn_rec *c)
{
    apr_status_t status;
    int mpm_state = 0;
    h2_session *session = h2_conn_ctx_get_session(c);
    
    ap_assert(session);
    do {
        if (c->cs) {
            c->cs->sense = CONN_SENSE_DEFAULT;
            c->cs->state = CONN_STATE_HANDLER;
        }
    
        status = h2_session_process(session, async_mpm);
        
        if (APR_STATUS_IS_EOF(status)) {
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, c, 
                          H2_SSSN_LOG(APLOGNO(03045), session, 
                          "process, closing conn"));
            c->keepalive = AP_CONN_CLOSE;
        }
        else {
            c->keepalive = AP_CONN_KEEPALIVE;
        }
        
        if (ap_mpm_query(AP_MPMQ_MPM_STATE, &mpm_state)) {
            break;
        }
    } while (!async_mpm
             && c->keepalive == AP_CONN_KEEPALIVE 
             && mpm_state != AP_MPMQ_STOPPING);

    if (c->cs) {
        switch (session->state) {
            case H2_SESSION_ST_INIT:
            case H2_SESSION_ST_IDLE:
            case H2_SESSION_ST_BUSY:
            case H2_SESSION_ST_WAIT:
                c->cs->state = CONN_STATE_WRITE_COMPLETION;
                if (c->cs && (session->open_streams || !session->remote.emitted_count)) {
                    /* let the MPM know that we are not done and want
                     * the Timeout behaviour instead of a KeepAliveTimeout
                     * See PR 63534. 
                     */
                    c->cs->sense = CONN_SENSE_WANT_READ;
                }
                break;
            case H2_SESSION_ST_CLEANUP:
            case H2_SESSION_ST_DONE:
            default:
                c->cs->state = CONN_STATE_LINGER;
            break;
        }
    }

    return APR_SUCCESS;
}

apr_status_t h2_c1_pre_close(struct h2_conn_ctx_t *ctx, conn_rec *c)
{
    h2_session *session = h2_conn_ctx_get_session(c);
    
    (void)c;
    if (session) {
        apr_status_t status = h2_session_pre_close(session, async_mpm);
        return (status == APR_SUCCESS)? DONE : status;
    }
    return DONE;
}

