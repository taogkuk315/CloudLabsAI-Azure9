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

#include <apr_strings.h>
#include <apr_optional.h>
#include <apr_optional_hooks.h>

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_connection.h>
#include <http_protocol.h>
#include <http_log.h>

#include "h2_private.h"

#include "h2_stream.h"
#include "h2_task.h"
#include "h2_config.h"
#include "h2_ctx.h"
#include "h2_conn.h"
#include "h2_h2.h"

const char *h2_protos[] = {
    "h2", "h2-16", "h2-14"
};
apr_size_t h2_protos_len = sizeof(h2_protos)/sizeof(h2_protos[0]);

/**
 * The optional mod_ssl functions we need. We want to compile without using
 * mod_ssl's header file.
 */
APR_DECLARE_OPTIONAL_FN(int, ssl_engine_disable, (conn_rec*));
APR_DECLARE_OPTIONAL_FN(int, ssl_is_https, (conn_rec*));
APR_DECLARE_OPTIONAL_FN(char *, ssl_var_lookup,
                        (apr_pool_t *, server_rec *,
                         conn_rec *, request_rec *,
                         char *));

typedef int (*ssl_npn_advertise_protos)(conn_rec *connection, 
                                      apr_array_header_t *protos);
typedef int (*ssl_npn_proto_negotiated)(conn_rec *connection, 
                                      const char *proto_name,
                                      apr_size_t proto_name_len);

APR_DECLARE_OPTIONAL_FN(int, modssl_register_npn, 
                        (conn_rec *conn,
                         ssl_npn_advertise_protos advertisefn,
                         ssl_npn_proto_negotiated negotiatedfn));

typedef int (*ssl_alpn_propose_protos)(conn_rec *connection,
apr_array_header_t *client_protos, apr_array_header_t *protos);

typedef int (*ssl_alpn_proto_negotiated)(conn_rec *connection,
const char *proto_name, apr_size_t proto_name_len);

APR_DECLARE_OPTIONAL_FN(int, modssl_register_alpn,
                        (conn_rec *conn,
                         ssl_alpn_propose_protos proposefn,
                         ssl_alpn_proto_negotiated negotiatedfn));

int h2_h2_post_read_req(request_rec *r);

static int (*opt_ssl_engine_disable)(conn_rec*);
static int (*opt_ssl_is_https)(conn_rec*);
static char *(*opt_ssl_var_lookup)(apr_pool_t *, server_rec *,
                                   conn_rec *, request_rec *,
                                   char *);
static int (*opt_ssl_register_alpn)(conn_rec*,
                                    ssl_alpn_propose_protos,
                                    ssl_alpn_proto_negotiated);
static int (*opt_ssl_register_npn)(conn_rec*,
                                    ssl_npn_advertise_protos,
                                    ssl_npn_proto_negotiated);

static const char *const mod_ssl[] = { "mod_ssl.c", NULL};
static const char *const more_core[] = { "core.c", NULL};

static void check_sni_host(conn_rec *c) 
{
    h2_ctx *ctx = h2_ctx_get(c, 1);
    if (opt_ssl_var_lookup && !ctx->hostname) {
        ctx->hostname = opt_ssl_var_lookup(c->pool, c->base_server, c, 
                                           NULL, "SSL_TLS_SNI");
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_h2, connection, SNI %s",
                      ctx->hostname? ctx->hostname : "NULL");
        
    }
}


void h2_h2_register_hooks(void)
{
    /* This hook runs on new connections before mod_ssl has a say.
     * Its purpose is to prevent mod_ssl from touching our pseudo-connections
     * for streams.
     */
    ap_hook_pre_connection(h2_h2_stream_pre_conn,
                           NULL, mod_ssl, APR_HOOK_FIRST);
    
    /* This hook runs on new connection after mod_ssl, but before the core
     * httpd. Its purpose is to register, if TLS is used, the ALPN callbacks
     * that enable us to chose "h2" as next procotol if the client supports it.
     */
    ap_hook_pre_connection(h2_h2_pre_conn, mod_ssl, more_core, APR_HOOK_LAST);
    
    /* When the connection processing actually starts, we might to
     * take over, if h2* was selected by ALPN on a TLS connection.
     */
    ap_hook_process_connection(h2_h2_process_conn, NULL, NULL, APR_HOOK_FIRST);
    
    ap_hook_post_read_request(h2_h2_post_read_req, NULL, NULL, APR_HOOK_MIDDLE);
}

apr_status_t h2_h2_init(apr_pool_t *pool, server_rec *s)
{
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, "h2_h2, child_init");
    opt_ssl_engine_disable = APR_RETRIEVE_OPTIONAL_FN(ssl_engine_disable);
    opt_ssl_is_https = APR_RETRIEVE_OPTIONAL_FN(ssl_is_https);
    opt_ssl_var_lookup = APR_RETRIEVE_OPTIONAL_FN(ssl_var_lookup);
    opt_ssl_register_npn = APR_RETRIEVE_OPTIONAL_FN(modssl_register_npn);
    opt_ssl_register_alpn = APR_RETRIEVE_OPTIONAL_FN(modssl_register_alpn);
    
    if (!opt_ssl_is_https) {
        ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s,
                     "mod_ssl does not seem to be enabled");
    }
    else if (!opt_ssl_register_alpn && !opt_ssl_register_npn) {
        ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s,
                     "mod_ssl does not offer ALPN or NPN registration");
    }
    return APR_SUCCESS;
}

apr_status_t h2_h2_child_init(apr_pool_t *pool, server_rec *s)
{
    return APR_SUCCESS;
}

int h2_h2_is_tls(conn_rec *c)
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

static int h2_h2_npn_advertise(conn_rec *c, apr_array_header_t *protos)
{
    check_sni_host(c);
    h2_config *cfg = h2_config_get(c);
    if (!h2_config_geti(cfg, H2_CONF_ENABLED)) {
        return DECLINED;
    }
    
    for (int i = 0; i < h2_protos_len; ++i) {
        const char *proto = h2_protos[i];
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "NPN proposing %s from client selection", proto);
        APR_ARRAY_PUSH(protos, const char*) = proto;
    }
    return OK;
}

static int h2_h2_npn_negotiated(conn_rec *c,
                                const char *proto_name,
                                apr_size_t proto_name_len)
{
    if (APLOGctrace1(c)) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "NPN negotiated is %s", 
                      apr_pstrndup(c->pool, proto_name, proto_name_len));
    }
    
    h2_config *cfg = h2_config_get(c);
    if (!h2_config_geti(cfg, H2_CONF_ENABLED)) {
        return DECLINED;
    }
    
    if (!h2_ctx_is_session(c) ) {
        return DECLINED;
    }
    
    if (h2_ctx_is_negotiated(c)) {
        // called twice? maybe alpn+npn overlap...
        return DECLINED;
    }
    
    for (int i = 0; i < h2_protos_len; ++i) {
        const char *proto = h2_protos[i];
        if (proto_name_len == strlen(proto)
            && strncmp(proto, proto_name, proto_name_len) == 0) {
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c, 
                          "protocol set va NPN to %s", proto);
            h2_ctx_set_protocol(c, proto);
            break;
        }
    }    
    return OK;
}

static int h2_h2_alpn_propose(conn_rec *c,
                              apr_array_header_t *client_protos,
                              apr_array_header_t *protos)
{
    check_sni_host(c);
    h2_config *cfg = h2_config_get(c);
    if (!h2_config_geti(cfg, H2_CONF_ENABLED)) {
        return DECLINED;
    }
    
    for (int i = 0; i < h2_protos_len; ++i) {
        const char *proto = h2_protos[i];
        if (h2_util_array_index(client_protos, proto) >= 0) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                          "ALPN proposing %s", proto);
            APR_ARRAY_PUSH(protos, const char*) = proto;
            return OK; /* propose only one, the first match from our list */
        }
    }
    return OK;
}

static int h2_h2_alpn_negotiated(conn_rec *c,
                                  const char *proto_name,
                                  apr_size_t proto_name_len)
{
    if (APLOGctrace1(c)) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "ALPN negotiated is %s", 
                      apr_pstrndup(c->pool, proto_name, proto_name_len));
    }
    
    h2_config *cfg = h2_config_get(c);
    if (!h2_config_geti(cfg, H2_CONF_ENABLED)) {
        return DECLINED;
    }
    
    if (!h2_ctx_is_session(c) ) {
        return DECLINED;
    }
    
    if (h2_ctx_is_negotiated(c)) {
        // called twice? maybe alpn+npn overlap...
        return DECLINED;
    }
    
    for (int i = 0; i < h2_protos_len; ++i) {
        const char *proto = h2_protos[i];
        if (proto_name_len == strlen(proto)
            && strncmp(proto, proto_name, proto_name_len) == 0) {
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c, 
                          "protocol set va ALPN to %s", proto);
            h2_ctx_set_protocol(c, proto);
            break;
        }
    }    
    return OK;
}

int h2_h2_pre_conn(conn_rec* c, void *arg)
{
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                  "h2_h2, pre_connection, start");
    h2_ctx *ctx = h2_ctx_get(c, 0);
    if (!ctx) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_h2, pre_connection, no ctx");
        /* We have not seen this one yet, are we active? */
        h2_config *cfg = h2_config_get(c);
        if (!h2_config_geti(cfg, H2_CONF_ENABLED)) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                          "h2_h2, pre_connection, h2 not enabled");
            return DECLINED;
        }
        
        /* Are we using TLS on this connection? */
        if (!h2_h2_is_tls(c)) {
            ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                          "h2_h2, pre_connection, no TLS");
            return DECLINED;
        }
        
        /* Does mod_ssl offer ALPN/NPN support? */
        if (opt_ssl_register_alpn == NULL && opt_ssl_register_npn == NULL) {
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, c,
                          "h2_h2, pre_connection, no ALPN/NPN support in mod_ssl");
            return DECLINED;
        }
        
        ctx = h2_ctx_get(c, 1);
        if (opt_ssl_register_alpn) {
            opt_ssl_register_alpn(c, h2_h2_alpn_propose, h2_h2_alpn_negotiated);
        }
        if (opt_ssl_register_npn) {
            opt_ssl_register_npn(c, h2_h2_npn_advertise, h2_h2_npn_negotiated);
        }

        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_h2, pre_connection, ALPN callback registered");
        
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_h2, pre_connection, end");
    }
    else if (h2_ctx_is_task(c)) {
        /* A connection that represents a http2 stream from another connection.
         */
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_h2, pre_connection, found stream task");
        h2_task *task = h2_ctx_get_task(ctx);
        return h2_task_pre_conn(task, c);
    }
    
    return DECLINED;
}

int h2_h2_process_conn(conn_rec* c)
{
    h2_ctx *ctx = h2_ctx_get(c, 0);
    
    if (ctx) {
        if (h2_ctx_is_task(c)) {
            // This should not happend, as we install our own filters
            // in h2_h2_pre_connection in such cases, so the normal
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
        check_sni_host(c);
    }
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c, "h2_h2, connection, start");
    if (h2_ctx_is_active(c)) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                      "h2_h2, connection, h2 active");
        
        return h2_conn_main(c);
    }
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, c,
                  "h2_h2, connection, declined");
    return DECLINED;
}

int h2_h2_stream_pre_conn(conn_rec* c, void *arg)
{
    h2_ctx *ctx = h2_ctx_get(c, 0);
    if (ctx && h2_ctx_is_task(c)) {
        /* This connection is a pseudo-connection used for a h2_task.
         * Since we read/write directly from it ourselves, we need
         * to disable a possible ssl connection filter.
         */
        if (opt_ssl_engine_disable) {
            opt_ssl_engine_disable(c);
        }
    }
    return OK;
}

int h2_h2_post_read_req(request_rec *r)
{
    h2_ctx *ctx = h2_ctx_rget(r, 0);
    struct h2_task *task = ctx? h2_ctx_get_task(ctx) : NULL;
    if (task) {
        /* h2_task connection for a stream, not for h2c */
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "adding h1_to_h2_resp output filter");
        ap_add_output_filter("H1_TO_H2_RESP", task, r, r->connection);
    }
    return DECLINED;
}


