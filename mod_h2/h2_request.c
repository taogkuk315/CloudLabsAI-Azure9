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

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_mplx.h"
#include "h2_to_h1.h"
#include "h2_request.h"


h2_request *h2_request_create(int id, apr_pool_t *pool)
{
    h2_request *req = apr_pcalloc(pool, sizeof(h2_request));
    if (req) {
        req->id = id;
        req->to_h1 = h2_to_h1_create(pool);
    }
    return req;
}

void h2_request_destroy(h2_request *req)
{
    if (req->to_h1) {
        h2_to_h1_destroy(req->to_h1);
        req->to_h1 = NULL;
    }
}

static apr_status_t insert_request_line(h2_request *req, h2_mplx *m);

struct whctx {
    h2_to_h1 *to_h1;
    h2_mplx *m;
};

static int write_header(void *puser, const char *key, const char *value)
{
    struct whctx *ctx = (struct whctx*)puser;
    apr_status_t status = h2_to_h1_add_header(ctx->to_h1,
                                              key, strlen(key),
                                              value, strlen(value),
                                              ctx->m);
    return status == APR_SUCCESS;
}

apr_status_t h2_request_rwrite(h2_request *req, request_rec *r,
                               h2_mplx *m, apr_pool_t *pool)
{
    req->method = r->method;
    req->path = r->uri;
    req->authority = r->hostname;
    req->scheme = NULL;
    
    ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r,
                  "h2_request(%d): writing request %s %s",
                  req->id, req->method, req->path);
    
    apr_status_t status = insert_request_line(req, m);
    req->started = 1;
    
    struct whctx ctx = { req->to_h1, m };
    apr_table_do(write_header, &ctx, r->headers_in, NULL);
    return status;
}

apr_status_t h2_request_write_header(h2_request *req,
                                     const char *name, size_t nlen,
                                     const char *value, size_t vlen,
                                     h2_mplx *m, apr_pool_t *pool)
{
    apr_status_t status = APR_SUCCESS;
    
    if (nlen <= 0) {
        return status;
    }
    
    if (name[0] == ':') {
        /* pseudo header, see ch. 8.1.2.3, always should come first */
        if (req->started) {
            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool,
                          "h2_request(%d): pseudo header after request start",
                          req->id);
            return APR_EGENERAL;
        }
        
        if (H2_HEADER_METHOD_LEN == nlen
            && !strncmp(H2_HEADER_METHOD, name, nlen)) {
            req->method = apr_pstrndup(pool, value, vlen);
        }
        else if (H2_HEADER_SCHEME_LEN == nlen
                 && !strncmp(H2_HEADER_SCHEME, name, nlen)) {
            req->scheme = apr_pstrndup(pool, value, vlen);
        }
        else if (H2_HEADER_PATH_LEN == nlen
                 && !strncmp(H2_HEADER_PATH, name, nlen)) {
            req->path = apr_pstrndup(pool, value, vlen);
        }
        else if (H2_HEADER_AUTH_LEN == nlen
                 && !strncmp(H2_HEADER_AUTH, name, nlen)) {
            req->authority = apr_pstrndup(pool, value, vlen);
        }
        else {
            char buffer[32];
            memset(buffer, 0, 32);
            strncpy(buffer, name, (nlen > 31)? 31 : nlen);
            ap_log_perror(APLOG_MARK, APLOG_INFO, 0, pool,
                          "h2_request(%d): ignoring unknown pseudo header %s",
                          req->id, buffer);
        }
    }
    else {
        /* non-pseudo header, append to work bucket of stream */
        if (!req->started) {
            apr_status_t status = insert_request_line(req, m);
            if (status != APR_SUCCESS) {
                return status;
            }
            req->started = 1;
        }
        
        if (status == APR_SUCCESS) {
            status = h2_to_h1_add_header(req->to_h1,
                                         name, nlen, value, vlen,
                                         m);
        }
    }
    
    return status;
}

apr_status_t h2_request_write_data(h2_request *req,
                                   const char *data, size_t len,
                                   struct h2_mplx *m)
{
    return h2_to_h1_add_data(req->to_h1, data, len, m);
}

apr_status_t h2_request_end_headers(h2_request *req, struct h2_mplx *m)
{
    if (!req->started) {
        apr_status_t status = insert_request_line(req, m);
        if (status != APR_SUCCESS) {
            return status;
        }
        req->started = 1;
    }
    return h2_to_h1_end_headers(req->to_h1, m);
}

apr_status_t h2_request_close(h2_request *req, struct h2_mplx *m)
{
    return h2_to_h1_close(req->to_h1, m);
}

h2_bucket *h2_request_steal_first_data(h2_request *req, int *peos)
{
    return h2_to_h1_steal_first_data(req->to_h1, peos);
}

static apr_status_t insert_request_line(h2_request *req, h2_mplx *m)
{
    return h2_to_h1_start_request(req->to_h1, req->id,
                                  req->method, req->path,
                                  req->authority, m);
}

apr_status_t h2_request_flush(h2_request *req, h2_mplx *m)
{
    return h2_to_h1_flush(req->to_h1, m);
}
