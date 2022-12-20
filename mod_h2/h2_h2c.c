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

#include <apr_optional.h>
#include <apr_optional_hooks.h>

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_connection.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>

#include "h2_private.h"
#include "h2_conn.h"
#include "h2_ctx.h"
#include "h2_h2.h"
#include "h2_h2c.h"
#include "h2_util.h"

const char *h2c_protos[] = {
    "h2c", "h2c-16", "h2c-14"
};
apr_size_t h2c_protos_len = sizeof(h2c_protos)/sizeof(h2c_protos[0]);

static int h2_h2c_request_handler(request_rec *r);
static const char *h2_get_upgrade_proto(request_rec *r);
static int h2_h2c_upgrade_to(request_rec *r, const char *proto);
static int h2_h2c_upgrade_options(request_rec *r);

void h2_h2c_register_hooks(void)
{
    ap_hook_handler(h2_h2c_request_handler, NULL, NULL, APR_HOOK_FIRST - 1);
    ap_hook_map_to_storage(h2_h2c_upgrade_options, NULL, NULL, APR_HOOK_FIRST);
}

static int h2_h2c_upgrade_options(request_rec *r)
{
    if ((r->method_number == M_OPTIONS) && r->uri && (r->uri[0] == '*') &&
        (r->uri[1] == '\0')) {
        ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r,
                      "h2c: request OPTIONS * seen");
        return h2_h2c_request_handler(r);
    }
    return DECLINED;
}

static int h2_h2c_request_handler(request_rec *r)
{
    if (h2_h2_is_tls(r->connection)) {
        /* h2c runs only on plain connections */
        return DECLINED;
    }
    if (h2_ctx_is_task(r->connection)) {
        /* h2_task connection for a stream, not for h2c */
        return DECLINED;
    }
    
    /* Check for the start of an h2c Upgrade dance. */
    const char *proto = h2_get_upgrade_proto(r);
    if (proto) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                     "seeing %s upgrade invitation", proto);
        /* We do not handle upgradeable requests with a body.
         * The reason being that we would need to read the body in full
         * before we ca use HTTP2 frames on the wire.
         */
        const char *clen = apr_table_get(r->headers_in, "Content-Length");
        if (clen && strcmp(clen, "0")) {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                          "upgrade with content-length: %s, declined", clen);
            return DECLINED;
        }
        return h2_h2c_upgrade_to(r, proto);
    }
    
    return DECLINED;
}

static const char *h2_get_upgrade_proto(request_rec *r)
{
    const char *upgrade = apr_table_get(r->headers_in, "Upgrade");
    const char *proto = h2_util_first_token_match(r->pool, upgrade, 
                                                  h2c_protos, h2c_protos_len);
    if (proto && 
        h2_util_contains_token(
            r->pool, apr_table_get(r->headers_in, "Connection"), "Upgrade")
        && apr_table_get(r->headers_in, "HTTP2-Settings")) {
        return proto;
    }
    if (upgrade) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "no suiteable upgrade detected: %s %s, "
                      "Upgrade: %s", r->method, r->uri, upgrade);
    }

    return NULL;
}

static int h2_h2c_upgrade_to(request_rec *r, const char *proto)
{
    h2_ctx *ctx = h2_ctx_rget(r, 1);
    ctx->is_h2 = 1;
    h2_ctx_set_protocol(r->connection, proto);
    
    /* Let the client know what we are upgrading to. */
    apr_table_clear(r->headers_out);
    apr_table_setn(r->headers_out, "Upgrade", proto);
    apr_table_setn(r->headers_out, "Connection", "Upgrade");
    
    r->status = HTTP_SWITCHING_PROTOCOLS;
    r->status_line = ap_get_status_line(r->status);
    ap_send_interim_response(r, 1);
    
    /* Make sure the core filter that parses http1 requests does
     * not mess with our http2 frames. */
    if (APLOGrtrace2(r)) {
        ap_filter_t *filter = r->input_filters;
        while (filter) {
            ap_log_rerror(APLOG_MARK, APLOG_TRACE2, 0, r,
                          "h2_conn(%ld), has request filter %s",
                          r->connection->id, filter->frec->name);
            filter = filter->next;
        }
    }
    ap_remove_input_filter_byhandle(r->input_filters, "http_in");
    ap_remove_input_filter_byhandle(r->input_filters, "reqtimeout");

    /* Ok, start an h2_conn on this one. */
    apr_status_t status = h2_conn_rprocess(r);
    if (status != DONE) {
        /* Nothing really to do about this. */
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, status, r,
                      "session proessed, unexpected status");
    }
    
    /* make sure httpd closes the connection after this */
    r->connection->keepalive = AP_CONN_CLOSE;
    ap_lingering_close(r->connection);
    
    return OK;
}

