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
#include <http_log.h>
#include <http_connection.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_bucket_queue.h"
#include "h2_response.h"

apr_status_t h2_response_init(h2_response *response, int stream_id, conn_rec *c)
{
    memset(response, 0, sizeof(h2_response));
    response->stream_id = stream_id;
    response->c = c;
    response->state = H2_RESP_ST_STATUS_LINE;
    return APR_SUCCESS;
}

h2_response *h2_response_create(int stream_id, conn_rec *c)
{
    h2_response *resp = apr_pcalloc(c->pool, sizeof(h2_response));
    h2_response_init(resp, stream_id, c);
    return resp;
}

apr_status_t h2_response_destroy(h2_response *response)
{
    if (response->rawhead) {
        h2_bucket_destroy(response->rawhead);
        response->rawhead = NULL;
    }
    return APR_SUCCESS;
}

static apr_status_t ensure_buffer(h2_response *resp)
{
    if (!resp->rawhead) {
        resp->rawhead = h2_bucket_alloc(16*1024);
        if (resp->rawhead == NULL) {
            return APR_ENOMEM;
        }
        resp->offset = 0;
    }
    return APR_SUCCESS;
}

static apr_size_t copy_body(h2_response *resp,
                            h2_bucket *bucket,
                            const char *data, apr_size_t len) {
    if (resp->chunked) {
        /* yikes, read the chunked body and pass on only the raw
         * data itself */
        // TODO
    }
    else {
        /* nothing to convert, just pass the bytes into bucket */
        return h2_bucket_append(bucket, data, len);
    }
    return 0;
}

static apr_status_t parse_headers(h2_response *resp)
{
    char *data = resp->rawhead->data;
    apr_size_t max = resp->rawhead->data_len - 1;
    for (int i = resp->offset; i < max; ++i) {
        if (data[i] == '\r' && data[i+1] == '\n') {
            if (i == resp->offset) {
                /* empty line -> end of headers */
                ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, resp->c,
                              "h2_response(%d): end of headers seen",
                              resp->stream_id);
                resp->offset = i + 2;
                resp->state = H2_RESP_ST_BODY;
                return APR_SUCCESS;
            }
            else {
                /* non-empty line -> header */
                data[i] = '\0';
                const char *line = data + resp->offset;
                resp->offset = i + 2;
                ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, resp->c,
                              "h2_response(%d): headers %s",
                              resp->stream_id, line);
            }
        }
    }
    
    /* No line end yet, wait for more data if we have not exceeded
     * our buffer capacities. Otherwise, report an error */
    if (h2_bucket_available(resp->rawhead) <= 0) {
        return APR_ENAMETOOLONG;
    }
    return APR_SUCCESS;
}

static apr_status_t parse_status_line(h2_response *resp)
{
    char *data = resp->rawhead->data;
    apr_size_t max = resp->rawhead->data_len - 1;
    for (int i = resp->offset; i < max; ++i) {
        if (data[i] == '\r' && data[i+1] == '\n') {
            /* found first line, make it a null-terminated string */
            const char *line = data + resp->offset;
            data[i] = '\0';
            char *s = strchr(line, ' ');
            if (s == NULL) {
                return APR_EINVAL;
            }
            while (*s == ' ') {
                ++s;
            }
            if (!*s) {
                return APR_EINVAL;
            }
            const char *sword = s;
            s = strchr(sword, ' ');
            if (s) {
                *s = '\0';
            }
            
            resp->status = apr_pstrdup(resp->c->pool, sword);
            resp->offset = i + 2;
            resp->state = H2_RESP_ST_HEADERS;
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, resp->c,
                          "h2_response(%d): parsed status %s",
                          resp->stream_id, resp->status);
            
            return parse_headers(resp);
        }
    }
    
    /* No line end yet, wait for more data if we have not exceeded
     * our buffer capacities. Otherwise, report an error */
    if (h2_bucket_available(resp->rawhead) <= 0) {
        return APR_ENAMETOOLONG;
    }
    return APR_SUCCESS;
}

apr_status_t h2_response_http_convert(h2_bucket *bucket,
                                      void *conv_ctx,
                                      const char *data, apr_size_t len,
                                      apr_size_t *pconsumed)
{
    h2_response *resp = (h2_response *)conv_ctx;
    apr_status_t status = APR_SUCCESS;
    *pconsumed = 0;
    
    
    if (len > 0) {
        switch (resp->state) {
            case H2_RESP_ST_STATUS_LINE:
            case H2_RESP_ST_HEADERS:
                status = ensure_buffer(resp);
                if (status != APR_SUCCESS) {
                    return status;
                }
                
                *pconsumed = h2_bucket_append(resp->rawhead, data, len);
                if (*pconsumed > 0) {
                    if (resp->state == H2_RESP_ST_STATUS_LINE) {
                        /* Need to parse a valid HTTP/1.1 status line here */
                        status = parse_status_line(resp);
                    }
                    
                    if (resp->state == H2_RESP_ST_HEADERS) {
                        status = parse_headers(resp);
                    }
                    
                    if (resp->state == H2_RESP_ST_BODY
                        && resp->rawhead
                        && resp->offset < resp->rawhead->data_len) {
                        apr_size_t remaining = resp->rawhead->data_len - resp->offset;
                        /* these bytes belong to the body */
                        assert(remaining < *pconsumed);
                        *pconsumed -= remaining;
                    }
                }
                break;
                
            case H2_RESP_ST_BODY:
                /* nothing to convert, just pass the bytes into bucket */
                *pconsumed = copy_body(resp, bucket, data, len);
                return APR_SUCCESS;
                
            default:
                /* ??? */
                break;
        }
    }
    return status;
}
