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
#include <stdio.h>

#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>
#include <http_connection.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_bucket_queue.h"
#include "h2_resp_head.h"
#include "h2_response.h"
#include "h2_util.h"

static void set_state(h2_response *resp, h2_response_state_t state)
{
    if (resp->state != state) {
        h2_response_state_t oldstate = resp->state;
        resp->state = state;
        if (resp->state_cb) {
            resp->state_cb(resp, oldstate, resp->state_cb_ctx);
        }
    }
}


h2_response *h2_response_create(int stream_id, conn_rec *c)
{
    h2_response *resp = apr_pcalloc(c->pool, sizeof(h2_response));
    if (resp) {
        resp->stream_id = stream_id;
        resp->c = c;
        resp->state = H2_RESP_ST_STATUS_LINE;
        resp->hlines = apr_array_make(c->pool, 10, sizeof(char *));
    }
    return resp;
}

apr_status_t h2_response_destroy(h2_response *response)
{
    set_state(response, H2_RESP_ST_DONE);
    if (response->rawhead) {
        h2_bucket_destroy(response->rawhead);
        response->rawhead = NULL;
    }
    if (response->head) {
        h2_resp_head_destroy(response->head);
        response->head = NULL;
    }
    if (response->chunk_work) {
        h2_bucket_destroy(response->chunk_work);
        response->chunk_work = NULL;
    }
    return APR_SUCCESS;
}

void h2_response_set_state_change_cb(h2_response *resp,
                                     h2_response_state_change_cb *callback,
                                     void *cb_ctx)
{
    resp->state_cb = callback;
    resp->state_cb_ctx = cb_ctx;
}

struct h2_resp_head *h2_response_get_head(h2_response *resp)
{
    h2_resp_head *head = resp->head;
    resp->head = NULL;
    return head;
}

static apr_status_t ensure_buffer(h2_response *resp)
{
    if (!resp->rawhead) {
        resp->rawhead = h2_bucket_alloc(BLOCKSIZE);
        if (resp->rawhead == NULL) {
            return APR_ENOMEM;
        }
        resp->offset = 0;
    }
    return APR_SUCCESS;
}

static apr_status_t make_h2_headers(h2_response *resp)
{
    resp->head = h2_resp_head_create(resp->rawhead, resp->stream_id, 
                                     resp->status, resp->hlines);
    if (resp->head == NULL) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EINVAL, resp->c,
                      "h2_response(%d): unable to create resp_head",
                      resp->stream_id);
        return APR_EINVAL;
    }
    
    resp->rawhead = NULL; /* h2_resp_head took ownership */
    
    for (int i = 1; i < resp->head->nvlen; ++i) {
        const nghttp2_nv *nv = &(&resp->head->nv)[i];
        
        if (!strcmp("transfer-encoding", (char*)nv->name)) {
            if (!strcmp("chunked", (char *)nv->value)) {
                resp->chunked = 1;
            }
        }
        else if (!resp->chunked && !strcmp("content-length", (char*)nv->name)) {
            apr_int64_t clen = apr_atoi64((char*)nv->value);
            if (clen <= 0) {
                ap_log_cerror(APLOG_MARK, APLOG_WARNING, APR_EINVAL, resp->c,
                              "h2_response(%d): content-length value not parsed: %s",
                              resp->stream_id, (char*)nv->value);
                return APR_EINVAL;
            }
            resp->body_len = clen;
        }
    }
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, resp->c,
                  "h2_response(%d): converted %d headers, content-length: %ld"
                  ", chunked=%d",
                  resp->stream_id, (int)resp->head->nvlen,
                  (long)resp->body_len, resp->chunked);
    
    resp->remain_len = resp->body_len;
    set_state(resp, ((resp->chunked || resp->body_len > 0)?
                     H2_RESP_ST_BODY : H2_RESP_ST_DONE));
    /* We are ready to be sent to the client */
    return APR_SUCCESS;
}

static apr_status_t parse_headers(h2_response *resp)
{
    char *data = resp->rawhead->data;
    apr_size_t max = resp->rawhead->data_len - 1;
    for (int i = resp->offset; i < max; ++i) {
        if (data[i] == '\r' && data[i+1] == '\n') {
            if (i == resp->offset) {
                /* empty line -> end of headers */
                ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, resp->c,
                              "h2_response(%d): end of headers",
                              resp->stream_id);
                resp->offset = i + 2;
                return make_h2_headers(resp);
            }
            else {
                /* non-empty line -> header, null-terminate it */
                data[i] = '\0';
                const char *line = data + resp->offset;
                resp->offset = i + 2;
                
                if (line[0] == ' ' || line[0] == '\t') {
                    /* continuation line from the header before this */
                    char *last = apr_array_pop(resp->hlines);
                    if (last == NULL) {
                        /* not well formed */
                        return APR_EINVAL;
                    }
                    char *last_eos = last + strlen(last);
                    memmove(last_eos, line, strlen(line)+1);
                    line = last;
                }
                /* new header line */
                APR_ARRAY_PUSH(resp->hlines, const char*) = line;
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
            
            resp->status = sword;
            resp->offset = i + 2;
            set_state(resp, H2_RESP_ST_HEADERS);
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, resp->c,
                          "h2_response(%d): status is %s",
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

static apr_status_t read_chunk_size(h2_response *resp,
                                    const char *data, apr_size_t len,
                                    apr_size_t *pconsumed) {
    if (!resp->chunk_work) {
        resp->chunk_work = h2_bucket_alloc(256);
        if (!resp->chunk_work) {
            return APR_ENOMEM;
        }
    }
    
    char *p = resp->chunk_work->data;
    apr_size_t p_start = resp->chunk_work->data_len;
    apr_size_t copied = h2_bucket_append(resp->chunk_work, data, len);
    
    if (copied > 0) {
        /* we have copied bytes, do we find a line end? */
        apr_size_t max = resp->chunk_work->data_len - 1;
        for (int i = 0; i < max; ++i) {
            if (p[i] == '\r' && p[i+1] == '\n') {
                /* how many bytes of the data have we consumed
                 * to find this line? */
                *pconsumed = ((long)i - p_start) + 2;
                /* null-terminate our chunk_work buffer */
                p[i] = '\0';
                char *end;
                resp->remain_len = apr_strtoi64(p, &end, 16);
                if (resp->remain_len == 0) {
                    if (p == end) {
                        /* invalid chunk size string */
                        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EINVAL, resp->c,
                                      "h2_response(%d): garbled chunk size: %s",
                                      resp->stream_id,
                                      apr_pstrndup(resp->c->pool, resp->chunk_work->data,
                                                   resp->chunk_work->data_len));
                        return APR_EINVAL;
                    }
                    /* valid chunk size 0, indicates end of body */
                    if (*pconsumed != len) {
                        ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, resp->c,
                                      "h2_response(%d): end chunk read, but %ld bytes remain",
                                      resp->stream_id, (long)len - *pconsumed);
                    }
                    set_state(resp, H2_RESP_ST_DONE);
                }
                h2_bucket_reset(resp->chunk_work);
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, resp->c,
                              "h2_response(%d): read chunk size %ld",
                              resp->stream_id, (long)resp->remain_len);
                break;
            }
        }
    }
    
    if (h2_bucket_available(resp->chunk_work) == 0) {
        /* our chunk_work buffer is full, yet we have not seen a
         * line end. Report an error. */
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EINVAL, resp->c,
                      "h2_response(%d): garbled chunk size: %s",
                      resp->stream_id,
                      apr_pstrndup(resp->c->pool, resp->chunk_work->data,
                                   resp->chunk_work->data_len));
        return APR_EINVAL;
    }
    
    *pconsumed = copied;
    return APR_SUCCESS;
}

static apr_status_t copy_body(h2_response *resp,
                              h2_bucket *bucket,
                              const char *data, apr_size_t len,
                              apr_size_t *pconsumed) {
    if (resp->chunked) {
        if (resp->remain_len > 0) {
            /* inside a chunk, copy out as much as we can */
            if (len > resp->remain_len) {
                len = resp->remain_len;
            }
            *pconsumed = h2_bucket_append(bucket, data, len);
            resp->remain_len -= *pconsumed;
        }
        
        if (resp->remain_len == 0) {
            apr_status_t status = read_chunk_size(resp, data, len, pconsumed);
            if (status != APR_SUCCESS) {
                return status;
            }
            
            if (resp->state == H2_RESP_ST_DONE) {
                /* that was the last, empty chunk that ended the body */
                return APR_SUCCESS;
            }
            
            if (resp->remain_len > 0) {
                /* we got a valid chunk size, copy the bytes */
                data += *pconsumed;
                len -= *pconsumed;
                if (len > 0) {
                    apr_size_t copied = 0;
                    status = copy_body(resp, bucket, data, len, &copied);
                    *pconsumed += copied;
                    return status;
                }
            }
            else {
                /* no valid chunk yet, need more bytes */
                assert(*pconsumed == len);
                return APR_SUCCESS;
            }
        }
    }
    else {
        /* nothing to convert, just pass the bytes into bucket */
        if (len > resp->remain_len) {
            /* body is longer then declared in headers */
            ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EINVAL, resp->c,
                          "h2_response(%d): body len %ld exceeded by %ld",
                          resp->stream_id, (long)resp->body_len,
                          (long)(resp->remain_len - len));
            return APR_EINVAL;
        }
        *pconsumed = h2_bucket_append(bucket, data, len);
        resp->remain_len -= *pconsumed;
        if (resp->remain_len == 0) {
            set_state(resp, H2_RESP_ST_DONE);
        }
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
                        apr_size_t avail = h2_bucket_available(resp->rawhead);
                        /* these bytes belong to the body */
                        assert(avail < *pconsumed);
                        *pconsumed -= avail;
                    }
                }
                break;
                
            case H2_RESP_ST_BODY:
                status = copy_body(resp, bucket, data, len, pconsumed);
                if (resp->state == H2_RESP_ST_DONE) {
                    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, resp->c,
                                  "h2_response(%d): body done",
                                  resp->stream_id);
                }
                break;
                
            case H2_RESP_ST_DONE:
                /* We get content after we were done, something is not
                 * right here */
                ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EINVAL, resp->c,
                              "h2_response(%d): body done, but receiving %ld more bytes",
                              resp->stream_id, (long)len);
                status = APR_EINVAL;
                break;
                
            default:
                /* ??? */
                break;
        }
    }
    return status;
}
