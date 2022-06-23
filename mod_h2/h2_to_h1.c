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
#include <stdio.h>

#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>
#include <http_connection.h>

#include "h2_private.h"
#include "h2_mplx.h"
#include "h2_response.h"
#include "h2_to_h1.h"
#include "h2_util.h"

#define HTTP_RLINE_SUFFIX       " HTTP/1.1\r\n"
#define HTTP_RLINE_SUFFIX_LEN   11

static const apr_off_t HEADERSIZE      = 16 * 1024;


struct h2_to_h1 {
    int stream_id;
    apr_pool_t *pool;
    h2_mplx *m;
    int eoh;
    int eos;
    int flushed;
    int seen_host;
    const char *authority;
    int chunked;
    apr_size_t remain_len;
    apr_table_t *headers;
    apr_bucket_brigade *bb;
};

h2_to_h1 *h2_to_h1_create(int stream_id, apr_pool_t *pool, 
                          apr_bucket_alloc_t *bucket_alloc, h2_mplx *m)
{
    h2_to_h1 *to_h1 = apr_pcalloc(pool, sizeof(h2_to_h1));
    if (to_h1) {
        to_h1->stream_id = stream_id;
        to_h1->pool = pool;
        to_h1->m = m;
        to_h1->headers = apr_table_make(to_h1->pool, 5);
        to_h1->bb = apr_brigade_create(pool, bucket_alloc);
    }
    return to_h1;
}

void h2_to_h1_destroy(h2_to_h1 *to_h1)
{
    to_h1->bb = NULL;
}

apr_status_t h2_to_h1_start_request(h2_to_h1 *to_h1,
                                    int stream_id,
                                    const char *method,
                                    const char *path,
                                    const char *authority)
{
    apr_status_t status = APR_SUCCESS;
    if (!method) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, h2_mplx_get_conn(to_h1->m),
                      "h2_to_h1: header start but :method missing");
        return APR_EGENERAL;
    }
    if (!path) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, h2_mplx_get_conn(to_h1->m),
                      "h2_to_h1: header start but :path missing");
        return APR_EGENERAL;
    }
    
    if (authority) {
        to_h1->authority = apr_pstrdup(to_h1->pool, authority);
    }
    
    status = apr_brigade_printf(to_h1->bb, NULL, NULL, 
                                "%s %s"HTTP_RLINE_SUFFIX, method, path);

    return status;
}


apr_status_t h2_to_h1_add_header(h2_to_h1 *to_h1,
                                 const char *name, size_t nlen,
                                 const char *value, size_t vlen)
{
    if (H2_HD_MATCH_LIT("transfer-encoding", name, nlen)) {
        if (!apr_strnatcasecmp("chunked", value)) {
            to_h1->chunked = 1;
        }
    }
    else if (H2_HD_MATCH_LIT("content-length", name, nlen)) {
        char *end;
        to_h1->remain_len = apr_strtoi64(value, &end, 10);
        if (value == end) {
            ap_log_cerror(APLOG_MARK, APLOG_WARNING, APR_EINVAL, 
                          h2_mplx_get_conn(to_h1->m),
                          "h2_request(%d): content-length value not parsed: %s",
                          to_h1->stream_id, value);
            return APR_EINVAL;
        }
    }
    else if ((to_h1->seen_host && H2_HD_MATCH_LIT("host", name, nlen))
             || H2_HD_MATCH_LIT("expect", name, nlen)
             || H2_HD_MATCH_LIT("upgrade", name, nlen)
             || H2_HD_MATCH_LIT("connection", name, nlen)
             || H2_HD_MATCH_LIT("proxy-connection", name, nlen)
             || H2_HD_MATCH_LIT("keep-alive", name, nlen)
             || H2_HD_MATCH_LIT("http2-settings", name, nlen)) {
        // ignore these.
        return APR_SUCCESS;
    }
    else if (H2_HD_MATCH_LIT("cookie", name, nlen)) {
        const char *existing = apr_table_get(to_h1->headers, "cookie");
        if (existing) {
            /* Cookie headers come separately in HTTP/2, but need
             * to be merged by "; " (instead of default ", ")
             */
            char *hvalue = apr_pstrndup(to_h1->pool, value, vlen);
            char *nval = apr_psprintf(to_h1->pool, "%s; %s", existing, hvalue);
            apr_table_setn(to_h1->headers, "Cookie", nval);
            return APR_SUCCESS;
        }
    }
    else if (H2_HD_MATCH_LIT("host", name, nlen)) {
        to_h1->seen_host = 1;
    }
    
    char *hname = apr_pstrndup(to_h1->pool, name, nlen);
    char *hvalue = apr_pstrndup(to_h1->pool, value, vlen);
    h2_util_camel_case_header(hname, nlen);
    apr_table_mergen(to_h1->headers, hname, hvalue);
    
    return APR_SUCCESS;
}


static int ser_header(void *ctx, const char *name, const char *value) 
{
    h2_to_h1 *to_h1 = (h2_to_h1*)ctx;
    apr_brigade_printf(to_h1->bb, NULL, NULL, "%s: %s\r\n", name, value);
    return 1;
}

static void serialize_headers(h2_to_h1 *to_h1) 
{
    apr_table_do(ser_header, to_h1, to_h1->headers, NULL);
}

apr_status_t h2_to_h1_end_headers(h2_to_h1 *to_h1)
{
    conn_rec *c = h2_mplx_get_conn(to_h1->m);
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                  "h2_to_h1(%ld-%d): end headers", 
                  h2_mplx_get_id(to_h1->m), to_h1->stream_id);
    
    if (to_h1->eoh) {
        return APR_EINVAL;
    }
    
    if (!to_h1->seen_host) {
        /* Need to add a "Host" header if not already there to
         * make virtual hosts work correctly. */
        if (!to_h1->authority) {
            return APR_BADARG;
        }
        apr_table_set(to_h1->headers, "Host", to_h1->authority);
    }

    serialize_headers(to_h1);
    apr_brigade_puts(to_h1->bb, NULL, NULL, "\r\n");
    
    if (APLOGctrace1(c)) {
        char buffer[1024];
        apr_size_t len = sizeof(buffer)-1;
        apr_brigade_flatten(to_h1->bb, buffer, &len);
        buffer[len] = 0;
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, c,
                      "h2_to_h1(%ld-%d): request is: %s", 
                      h2_mplx_get_id(to_h1->m), to_h1->stream_id, 
                      buffer);
    }
    
    apr_status_t status = h2_to_h1_flush(to_h1);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_WARNING, status, c,
                      "h2_to_h1(%ld-%d): end headers. flush", 
                      h2_mplx_get_id(to_h1->m), to_h1->stream_id);
    }
    
    to_h1->eoh = 1;

    return status;
}

static apr_status_t h2_to_h1_add_data_raw(h2_to_h1 *to_h1,
                                          const char *data, size_t len)
{
    apr_status_t status = APR_SUCCESS;
    conn_rec *c = h2_mplx_get_conn(to_h1->m);

    if (to_h1->eos || !to_h1->eoh) {
        return APR_EINVAL;
    }
    
    status = apr_brigade_write(to_h1->bb, NULL, NULL, data, len);
    if (status == APR_SUCCESS) {
        status = h2_to_h1_flush(to_h1);
    }
    return status;
}


apr_status_t h2_to_h1_add_data(h2_to_h1 *to_h1,
                               const char *data, size_t len)
{
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, h2_mplx_get_conn(to_h1->m),
                  "h2_to_h1(%d): add %ld data bytes", 
                  to_h1->stream_id, (long)len);
    
    if (to_h1->chunked) {
        /* if input may have a body and we have not seen any
         * content-length header, we need to chunk the input data.
         */
        apr_status_t status = apr_brigade_printf(to_h1->bb, NULL, NULL,
                                                 "%lx\r\n", len);
        if (status == APR_SUCCESS) {
            status = h2_to_h1_add_data_raw(to_h1, data, len);
            if (status == APR_SUCCESS) {
                status = apr_brigade_puts(to_h1->bb, NULL, NULL, "\r\n");
            }
        }
        return status;
    }
    
    return h2_to_h1_add_data_raw(to_h1, data, len);
}

apr_status_t h2_to_h1_flush(h2_to_h1 *to_h1)
{
    apr_status_t status = APR_SUCCESS;
    if (!APR_BRIGADE_EMPTY(to_h1->bb)) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, h2_mplx_get_conn(to_h1->m),
                      "h2_to_h1(%ld-%d): flush request bytes", 
                      h2_mplx_get_id(to_h1->m), to_h1->stream_id);
        
        status = h2_mplx_in_write(to_h1->m, to_h1->stream_id, to_h1->bb);
        if (status != APR_SUCCESS) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, status,
                          h2_mplx_get_conn(to_h1->m),
                          "h2_request(%d): pushing request data",
                          to_h1->stream_id);
        }
    }
    return status;
}

apr_status_t h2_to_h1_close(h2_to_h1 *to_h1)
{
    apr_status_t status = APR_SUCCESS;
    if (!to_h1->eos) {
        to_h1->eos = 1;
        if (to_h1->chunked) {
            status = h2_to_h1_add_data_raw(to_h1, "0\r\n\r\n", 5);
        }
        status = h2_to_h1_flush(to_h1);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, h2_mplx_get_conn(to_h1->m),
                      "h2_to_h1(%d): close", to_h1->stream_id);
        
        status = h2_mplx_in_close(to_h1->m, to_h1->stream_id);
    }
    return status;
}


