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

#include "h2_private.h"
#include "h2_conn.h"
#include "h2_ctx.h"
#include "h2_h2.h"
#include "h2_h2c.h"
#include "h2_util.h"


static int h2_h2c_request_handler(request_rec *r);
static int h2_h2c_is_upgrade(request_rec *r);
static int h2_h2c_upgrade_to(request_rec *r, const char *proto);

void h2_h2c_register_hooks(void)
{
    ap_hook_handler(h2_h2c_request_handler, NULL, NULL, APR_HOOK_FIRST - 1);
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
    if (h2_h2c_is_upgrade(r)) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                     "seeing h2c upgrade invitation");
        /* We do not handle upgradeable requests with a body.
         * The reason being that we would need to read the body in full
         * before we ca use HTTP2 frames on the wire.
         */
        const char *clen = apr_table_get(r->headers_in, "Content-Length");
        if (clen && strcmp(clen, "0")) {
            return DECLINED;
        }
        return h2_h2c_upgrade_to(r, "h2c-14");
    }
    
    return DECLINED;
}

static int h2_h2c_is_upgrade(request_rec *r)
{
    return (h2_util_contains_token(
            r->pool, apr_table_get(r->headers_in, "Upgrade"), "h2c-14")
        && h2_util_contains_token(
            r->pool, apr_table_get(r->headers_in, "Connection"), "Upgrade")
        && apr_table_get(r->headers_in, "HTTP2-Settings"));
}

static int h2_h2c_upgrade_to(request_rec *r, const char *proto)
{
    /* Let the client know what we are upgrading to. */
    apr_table_clear(r->headers_out);
    apr_table_setn(r->headers_out, "Upgrade", proto);
    apr_table_setn(r->headers_out, "Connection", "Upgrade");
    
    r->status = HTTP_SWITCHING_PROTOCOLS;
    r->status_line = ap_get_status_line(r->status);
    ap_send_interim_response(r, 1);
    
    /* Make sure the core filter that parses http1 requests does
     * not mess with our http2 frames. */
    ap_remove_input_filter_byhandle(r->input_filters, "http_in");

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
