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
#include "h2_request.h"

#define HTTP_RLINE_SUFFIX       " HTTP/1.1\r\n"
#define HTTP_RLINE_SUFFIX_LEN   11


static apr_status_t h2_req_head_add_start(h2_bucket *bucket,
                                          const char *method, const char *path);
static apr_status_t h2_req_head_add_header(h2_request *req, h2_mplx *m,
                                           const char *name, size_t nlen,
                                           const char *value, size_t vlen);
static apr_status_t ensure_work(h2_request *req, apr_size_t size);
static apr_status_t insert_request_line(h2_request *req, h2_mplx *m);


void h2_request_init(h2_request *req, int id)
{
    memset(req, 0, sizeof(h2_request));
    req->id = id;
}

struct whctx {
    h2_request *req;
    h2_mplx *m;
};

static int write_header(void *puser, const char *key, const char *value)
{
    struct whctx *ctx = (struct whctx*)puser;
    apr_status_t status = h2_req_head_add_header(ctx->req, ctx->m,
                                                 key, strlen(key),
                                                 value, strlen(value));
    return status == APR_SUCCESS;
}

apr_status_t h2_request_rwrite(h2_request *req, request_rec *r,
                               h2_mplx *m, apr_pool_t *pool)
{
    req->method = r->method;
    req->path = r->uri;
    req->authority = r->hostname;
    req->scheme = NULL;
    
    apr_status_t status = insert_request_line(req, m);
    req->started = 1;
    
    struct whctx ctx = { req, m };
    apr_table_do(write_header, &ctx, r->headers_in, NULL);
    
    if (status == APR_SUCCESS) {
        status = h2_request_end_headers(req, m);
    }
    return status;
}

void h2_request_destroy(h2_request *req)
{
    if (req->http1) {
        h2_bucket_destroy(req->http1);
        req->http1 = NULL;
    }
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
        if (req->http1) {
            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool,
                          "h2_request(%d): pseudo header after request start",
                          req->id);
            return APR_EGENERAL;
        }
        
        if (vlen <= 0) {
            char buffer[32];
            memset(buffer, 0, 32);
            strncpy(buffer, name, (nlen > 31)? 31 : nlen);
            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pool,
                          "h2_request(%d): pseudo header without value %s",
                          req->id, buffer);
            status = APR_EGENERAL;
        }
        else if (H2_HEADER_METHOD_LEN == nlen
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
            status = insert_request_line(req, m);
            req->started = 1;
        }
        
        if (status == APR_SUCCESS) {
            status = h2_req_head_add_header(req, m, name, nlen, value, vlen);
        }
    }
    
    return status;
}

apr_status_t h2_request_write_data(h2_request *req,
                                   const char *data, size_t len,
                                   struct h2_mplx *m)
{
    if (req->eos || !req->eoh) {
        return NGHTTP2_ERR_INVALID_STREAM_STATE;
    }
    apr_status_t status = ensure_work(req, DATA_BLOCKSIZE);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    while (len > 0) {
        apr_size_t written = h2_bucket_append(req->http1, data, len);
        if (written < len) {
            len -= written;
            data += written;
            apr_status_t status = h2_request_flush(req, m);
            if (status != APR_SUCCESS) {
                return status;
            }
        }
        else {
            len = 0;
        }
    }
    return APR_SUCCESS;
}

apr_status_t h2_request_end_headers(h2_request *req, struct h2_mplx *m)
{
    apr_status_t status = ensure_work(req, BLOCKSIZE);
    if (status != APR_SUCCESS) {
        return status;
    }
    req->eoh = 1;
    
    if (!req->started) {
        status = insert_request_line(req, m);
        req->started = 1;
    }
    
    if (!h2_bucket_has_free(req->http1, 2)) {
        status = h2_request_flush(req, m);
    }
    
    if (status == APR_SUCCESS) {
        h2_bucket_cat(req->http1, "\r\n");
    }
    return status;
}



apr_status_t h2_request_close(h2_request *req, struct h2_mplx *m)
{
    apr_status_t status = APR_SUCCESS;
    if (!req->eos) {
        status = h2_request_flush(req, m);
        req->eos = 1;
        if (status == APR_SUCCESS) {
            status = h2_mplx_in_close(m, req->id);
        }
    }
    return status;
}

static apr_status_t h2_req_head_add_start(h2_bucket *bucket,
                                          const char *method, const char *path)
{
    size_t mlen = strlen(method);
    size_t plen = strlen(path);
    size_t total = mlen + 1 + plen + HTTP_RLINE_SUFFIX_LEN;
    if (!h2_bucket_has_free(bucket, total)) {
        return APR_ENAMETOOLONG;
    }
    h2_bucket_append(bucket, method, mlen);
    h2_bucket_append(bucket, " ", 1);
    h2_bucket_append(bucket, path, plen);
    h2_bucket_append(bucket, HTTP_RLINE_SUFFIX, HTTP_RLINE_SUFFIX_LEN);
    return APR_SUCCESS;
}

static apr_status_t h2_req_head_add_header_int(h2_bucket *bucket,
                                               const char *name, size_t nlen,
                                               const char *value, size_t vlen)
{
    if (nlen > 0) {
        size_t total = nlen + vlen + 4;
        if (!h2_bucket_has_free(bucket, total)) {
            return APR_ENAMETOOLONG;
        }
        h2_bucket_append(bucket, name, nlen);
        h2_bucket_append(bucket, ": ", 2);
        if (vlen > 0) {
            h2_bucket_append(bucket, value, vlen);
        }
        h2_bucket_append(bucket, "\r\n", 2);
    }
    return APR_SUCCESS;
}

static apr_status_t h2_req_head_add_header(h2_request *req,
                                           h2_mplx *m,
                                           const char *name, size_t nlen,
                                           const char *value, size_t vlen)
{
    apr_status_t status = h2_req_head_add_header_int(req->http1,
                                                     name, nlen, value, vlen);
    if (status == APR_ENAMETOOLONG && req->http1->data_len > 0) {
        /* header did not fit into bucket, push bucket to input and
         * get a new one */
        status = h2_request_flush(req, m);
        if (status == APR_SUCCESS) {
            status = h2_req_head_add_header_int(req->http1,
                                                name, nlen, value, vlen);
            /* if this still does not work, we fail */
        }
    }
    return status;
}

static apr_status_t ensure_work(h2_request *req, apr_size_t size)
{
    if (!req->http1) {
        req->http1 = h2_bucket_alloc(size);
        if (!req->http1) {
            return APR_ENOMEM;
        }
    }
    return APR_SUCCESS;
}

apr_status_t h2_request_flush(h2_request *req, struct h2_mplx *m)
{
    apr_status_t status = APR_SUCCESS;
    if (req->http1) {
        status = h2_mplx_in_write(m, req->id, req->http1);
        req->http1 = NULL;
        req->flushed = 1;
        if (status != APR_SUCCESS) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, status,
                          h2_mplx_get_connection(m),
                          "h2_request(%d): pushing request data", req->id);
        }
    }
    return status;
}

h2_bucket *h2_request_get_http1_start(h2_request *req)
{
    if (!req->flushed && req->http1) {
        h2_bucket *data = req->http1;
        req->http1 = NULL;
        req->flushed = 1;
        return data;
    }
    return NULL;
}

static apr_status_t insert_request_line(h2_request *req, h2_mplx *m)
{
    apr_status_t status = APR_SUCCESS;
    if (!req->method) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, h2_mplx_get_connection(m),
                      "h2_request(%d): header start but :method missing",
                      req->id);
        return APR_EGENERAL;
    }
    if (!req->path) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, h2_mplx_get_connection(m),
                      "h2_request(%d): header start but :path missing",
                      req->id);
        return APR_EGENERAL;
    }
    
    status = ensure_work(req, BLOCKSIZE);
    if (status != APR_SUCCESS) {
        return status;
    }
    status = h2_req_head_add_start(req->http1, req->method, req->path);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, h2_mplx_get_connection(m),
                      "h2_request(%d): adding request line",
                      req->id);
    }
    if (req->authority) {
        status = h2_req_head_add_header(req, m,
                                        "Host", 4,
                                        req->authority,
                                        strlen(req->authority));
    }
    return status;
}

