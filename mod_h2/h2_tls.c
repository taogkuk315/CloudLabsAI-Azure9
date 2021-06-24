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

#include "h2_stream.h"
#include "h2_task.h"
#include "h2_config.h"
#include "h2_ctx.h"
#include "h2_conn.h"
#include "h2_tls.h"


/**
 * The optional mod_ssl functions we need. We want to compile without using
 * mod_ssl's header file.
 */
APR_DECLARE_OPTIONAL_FN(int, ssl_engine_disable, (conn_rec*));
APR_DECLARE_OPTIONAL_FN(int, ssl_is_https, (conn_rec*));

typedef int (*ssl_alpn_propose_protos)(conn_rec *connection,
apr_array_header_t *client_protos, apr_array_header_t *protos);

typedef int (*ssl_alpn_proto_negotiated)(conn_rec *connection,
const char *proto_name, apr_size_t proto_name_len);

APR_DECLARE_OPTIONAL_FN(int, modssl_register_alpn,
                        (conn_rec *conn,
                         ssl_alpn_propose_protos proposefn,
                         ssl_alpn_proto_negotiated negotiatedfn));


static int (*opt_ssl_engine_disable)(conn_rec*);
static int (*opt_ssl_is_https)(conn_rec*);
static int (*opt_ssl_register_alpn)(conn_rec*,
                                    ssl_alpn_propose_protos,
                                    ssl_alpn_proto_negotiated);

apr_status_t h2_tls_init(apr_pool_t *pool, server_rec *s)
{
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, "h2_tls, child_init");
    opt_ssl_engine_disable = APR_RETRIEVE_OPTIONAL_FN(ssl_engine_disable);
    opt_ssl_is_https = APR_RETRIEVE_OPTIONAL_FN(ssl_is_https);
    opt_ssl_register_alpn = APR_RETRIEVE_OPTIONAL_FN(modssl_register_alpn);
    
    if (!opt_ssl_is_https) {
        ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s,
                     "mod_ssl does not seem to be enabled");
    }
    else if (!opt_ssl_register_alpn) {
        ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s,
                     "mod_ssl does not offer ALPN registration");
    }
    return APR_SUCCESS;
}

apr_status_t h2_tls_child_init(apr_pool_t *pool, server_rec *s)
{
    return APR_SUCCESS;
}

int h2_tls_is_tls(conn_rec *c)
{
    return opt_ssl_is_https && opt_ssl_is_https(c);
}


static int h2_util_array_index(apr_array_header_t *array, const char *s)
{
    for (int i = 0; i < array->nelts; i++) {
        const char *p = APR_ARRAY_IDX(array, i, const char*);
        if (!strcmp(p, s)) {
            return i;
        }
    }
    return -1;
}

static int h2_tls_alpn_propose(conn_rec *c,
                               apr_array_header_t *client_protos,
                               apr_array_header_t *protos)
{
    h2_config *cfg = h2_config_get(c);
    if (!h2_config_geti(cfg, H2_CONF_ENABLED)) {
        return DECLINED;
    }
    
    if (!client_protos
        || h2_util_array_index(client_protos, PROTO_H2_14) >= 0) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "ALPN proposing %s", PROTO_H2_14);
        APR_ARRAY_PUSH(protos, const char*) = PROTO_H2_14;
    }
    return OK;
}

static int h2_tls_alpn_negotiated(conn_rec *c,
                                  const char *proto_name,
                                  apr_size_t proto_name_len)
{
    h2_config *cfg = h2_config_get(c);
    if (!h2_config_geti(cfg, H2_CONF_ENABLED)) {
        return DECLINED;
    }
    
    if (!h2_ctx_is_session(c) ) {
        return DECLINED;
    }
    
    if (h2_ctx_is_negotiated(c)) {
        // called twice? should not happen...
        return DECLINED;
    }
    
    if (proto_name_len == strlen(PROTO_H2_14)
        && strncmp(PROTO_H2_14, proto_name, proto_name_len) == 0) {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c, "ALPN negotiated: h2-14");
        h2_ctx_set_protocol(c, PROTO_H2_14);
    }
    else {
        h2_ctx_set_protocol(c, NULL);
    }
    return OK;
}

int h2_tls_pre_conn(conn_rec* c, void *arg)
{
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                  "h2_tls, pre_connection, start");
    h2_ctx *ctx = h2_ctx_get(c);
    if (!ctx) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_tls, pre_connection, no ctx");
        /* We have not seen this one yet, are we active? */
        h2_config *cfg = h2_config_get(c);
        if (!h2_config_geti(cfg, H2_CONF_ENABLED)) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                          "h2_tls, pre_connection, h2 not enabled");
            return DECLINED;
        }
        
        /* Are we using TLS on this connection? */
        if (!h2_tls_is_tls(c)) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                          "h2_tls, pre_connection, no TLS");
            return DECLINED;
        }
        
        /* Does mod_ssl offer ALPN support? */
        if (opt_ssl_register_alpn == NULL) {
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c,
                          "h2_tls, pre_connection, no ALPN support in mod_ssl");
            return DECLINED;
        }
        
        ctx = h2_ctx_create(c);
        opt_ssl_register_alpn(c, h2_tls_alpn_propose, h2_tls_alpn_negotiated);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_tls, pre_connection, ALPN callback registered");
        
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_tls, pre_connection, end");
    }
    else if (h2_ctx_is_stream(c)) {
        /* A connection that represents a http2 stream from another connection.
         */
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_tls, pre_connection, found stream task");
        h2_task *task = (h2_task *)ctx->userp;
        return h2_task_pre_conn(task, c);
    }
    
    return DECLINED;
}

int h2_tls_process_conn(conn_rec* c)
{
    h2_ctx *ctx = h2_ctx_get(c);
    
    if (ctx) {
        if (h2_ctx_is_stream(c)) {
            // This should not happend, as we install our own filters
            // in h2_tls_pre_connection in such cases, so the normal
            // connection hooks get bypassed.
            return DECLINED;
        }
        else if (!h2_ctx_is_negotiated(c)) {
            // Let the client/server hellos fly and ALPN call us back.
            apr_bucket_brigade* temp_brigade = apr_brigade_create(
                c->pool, c->bucket_alloc);
            const apr_status_t status = ap_get_brigade(c->input_filters,
                temp_brigade, AP_MODE_SPECULATIVE, APR_BLOCK_READ, 1);
            apr_brigade_destroy(temp_brigade);
        }
    }
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c, "h2_tls, connection, start");
    if (h2_ctx_is_active(c)) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_tls, connection, h2 active");
        
        return h2_conn_process(c);
    }
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                  "h2_tls, connection, declined");
    return DECLINED;
}

int h2_tls_stream_pre_conn(conn_rec* c, void *arg)
{
    h2_ctx *ctx = h2_ctx_get(c);
    if (ctx && h2_ctx_is_stream(c)) {
        /* This connection is a pseudo-connection used for a stream.
         * And it uses TLS by default. We need to disable that.
         */
        if (!opt_ssl_engine_disable || opt_ssl_engine_disable(c) == 0) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, c,
                          "h2_tls, stream_pre_conn, unable to disable TLS");
        }
    }
    return OK;
}

