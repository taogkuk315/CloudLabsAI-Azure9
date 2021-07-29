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
#include "h2_response.h"
#include "h2_from_h1.h"
#include "h2_util.h"

struct h2_from_h1 {
    int stream_id;
    h2_from_h1_state_t state;
    apr_pool_t *pool;
    
    h2_from_h1_state_change_cb *state_cb;
    void *state_cb_ctx;

    int chunked;
    apr_size_t remain_len;
    struct h2_bucket *chunk_work;
    
    apr_size_t offset;
    struct h2_bucket *rawhead;
    
    const char *status;
    apr_array_header_t *hlines;
    
    struct h2_response *head;
};

static void set_state(h2_from_h1 *from_h1, h2_from_h1_state_t state);

h2_from_h1 *h2_from_h1_create(int stream_id, apr_pool_t *pool)
{
    h2_from_h1 *from_h1 = apr_pcalloc(pool, sizeof(h2_from_h1));
    if (from_h1) {
        from_h1->stream_id = stream_id;
        from_h1->pool = pool;
        from_h1->state = H2_RESP_ST_STATUS_LINE;
        from_h1->hlines = apr_array_make(pool, 10, sizeof(char *));
    }
    return from_h1;
}

apr_status_t h2_from_h1_destroy(h2_from_h1 *from_h1)
{
    if (from_h1->rawhead) {
        h2_bucket_destroy(from_h1->rawhead);
        from_h1->rawhead = NULL;
    }
    if (from_h1->head) {
        h2_response_destroy(from_h1->head);
        from_h1->head = NULL;
    }
    if (from_h1->chunk_work) {
        h2_bucket_destroy(from_h1->chunk_work);
        from_h1->chunk_work = NULL;
    }
    return APR_SUCCESS;
}

h2_from_h1_state_t h2_from_h1_get_state(h2_from_h1 *from_h1)
{
    return from_h1->state;
}

static void set_state(h2_from_h1 *from_h1, h2_from_h1_state_t state)
{
    if (from_h1->state != state) {
        h2_from_h1_state_t oldstate = from_h1->state;
        from_h1->state = state;
        if (from_h1->state_cb) {
            from_h1->state_cb(from_h1, oldstate, from_h1->state_cb_ctx);
        }
    }
}

void h2_from_h1_set_state_change_cb(h2_from_h1 *from_h1,
                                     h2_from_h1_state_change_cb *callback,
                                     void *cb_ctx)
{
    from_h1->state_cb = callback;
    from_h1->state_cb_ctx = cb_ctx;
}

h2_response *h2_from_h1_get_response(h2_from_h1 *from_h1)
{
    h2_response *head = from_h1->head;
    from_h1->head = NULL;
    return head;
}

static apr_status_t ensure_buffer(h2_from_h1 *from_h1)
{
    if (!from_h1->rawhead) {
        from_h1->rawhead = h2_bucket_alloc(BLOCKSIZE);
        if (from_h1->rawhead == NULL) {
            return APR_ENOMEM;
        }
        from_h1->offset = 0;
    }
    return APR_SUCCESS;
}

static apr_status_t make_h2_headers(h2_from_h1 *from_h1)
{
    from_h1->head = h2_response_create(from_h1->stream_id, APR_SUCCESS,
                                     from_h1->status, from_h1->hlines,
                                     from_h1->rawhead, from_h1->pool);
    if (from_h1->head == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, APR_EINVAL, from_h1->pool,
                      "h2_from_h1(%d): unable to create resp_head",
                      from_h1->stream_id);
        return APR_EINVAL;
    }
    from_h1->rawhead = NULL; /* h2_response took ownership */
    
    from_h1->remain_len = h2_response_get_content_length(from_h1->head);
    from_h1->chunked = from_h1->head->chunked;
    
    ap_log_perror(APLOG_MARK, APLOG_TRACE2, 0, from_h1->pool,
                  "h2_from_h1(%d): converted %d headers, content-length: %ld"
                  ", chunked=%d",
                  from_h1->stream_id, (int)from_h1->head->nvlen,
                  (long)from_h1->remain_len, from_h1->chunked);
    
    set_state(from_h1, ((from_h1->chunked || from_h1->remain_len > 0)?
                     H2_RESP_ST_BODY : H2_RESP_ST_DONE));
    /* We are ready to be sent to the client */
    return APR_SUCCESS;
}

static apr_status_t parse_headers(h2_from_h1 *from_h1)
{
    char *data = from_h1->rawhead->data;
    apr_size_t max = from_h1->rawhead->data_len - 1;
    for (int i = from_h1->offset; i < max; ++i) {
        if (data[i] == '\r' && data[i+1] == '\n') {
            if (i == from_h1->offset) {
                /* empty line -> end of headers */
                ap_log_perror(APLOG_MARK, APLOG_TRACE1, 0, from_h1->pool,
                              "h2_from_h1(%d): end of headers",
                              from_h1->stream_id);
                from_h1->offset += 2;
                return make_h2_headers(from_h1);
            }
            else {
                /* non-empty line -> header, null-terminate it */
                data[i] = '\0';
                const char *line = data + from_h1->offset;
                from_h1->offset = i + 2;
                
                if (line[0] == ' ' || line[0] == '\t') {
                    /* continuation line from the header before this */
                    char *last = apr_array_pop(from_h1->hlines);
                    if (last == NULL) {
                        /* not well formed */
                        return APR_EINVAL;
                    }
                    char *last_eos = last + strlen(last);
                    memmove(last_eos, line, strlen(line)+1);
                    line = last;
                }
                /* new header line */
                APR_ARRAY_PUSH(from_h1->hlines, const char*) = line;
            }
        }
    }
    
    /* No line end yet, wait for more data if we have not exceeded
     * our buffer capacities. Otherwise, report an error */
    if (h2_bucket_available(from_h1->rawhead) <= 0) {
        return APR_ENAMETOOLONG;
    }
    return APR_SUCCESS;
}

static apr_status_t parse_status_line(h2_from_h1 *from_h1)
{
    char *data = from_h1->rawhead->data;
    apr_size_t max = from_h1->rawhead->data_len - 1;
    for (int i = from_h1->offset; i < max; ++i) {
        if (data[i] == '\r' && data[i+1] == '\n') {
            /* found first line, make it a null-terminated string */
            const char *line = data + from_h1->offset;
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
            
            from_h1->status = sword;
            from_h1->offset = i + 2;
            set_state(from_h1, H2_RESP_ST_HEADERS);
            ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, from_h1->pool,
                          "h2_from_h1(%d): status is %s",
                          from_h1->stream_id, from_h1->status);
            
            return parse_headers(from_h1);
        }
    }
    
    /* No line end yet, wait for more data if we have not exceeded
     * our buffer capacities. Otherwise, report an error */
    if (h2_bucket_available(from_h1->rawhead) <= 0) {
        return APR_ENAMETOOLONG;
    }
    return APR_SUCCESS;
}

static apr_status_t read_chunk_size(h2_from_h1 *from_h1,
                                    const char *data, apr_size_t len,
                                    apr_size_t *pconsumed) {
    if (!from_h1->chunk_work) {
        from_h1->chunk_work = h2_bucket_alloc(256);
        if (!from_h1->chunk_work) {
            return APR_ENOMEM;
        }
    }
    
    char *p = from_h1->chunk_work->data;
    apr_size_t p_start = from_h1->chunk_work->data_len;
    apr_size_t copied = h2_bucket_append(from_h1->chunk_work, data, len);
    
    if (copied > 0) {
        /* we have copied bytes, do we find a line end? */
        apr_size_t max = from_h1->chunk_work->data_len - 1;
        for (int i = 0; i < max; ++i) {
            if (p[i] == '\r' && p[i+1] == '\n') {
                /* how many bytes of the data have we consumed
                 * to find this line? */
                *pconsumed = ((long)i - p_start) + 2;
                /* null-terminate our chunk_work buffer */
                p[i] = '\0';
                char *end;
                from_h1->remain_len = apr_strtoi64(p, &end, 16);
                if (from_h1->remain_len == 0) {
                    if (p == end) {
                        /* invalid chunk size string */
                        ap_log_perror(APLOG_MARK, APLOG_ERR, APR_EINVAL, from_h1->pool,
                                      "h2_from_h1(%d): garbled chunk size: %s",
                                      from_h1->stream_id,
                                      apr_pstrndup(from_h1->pool, from_h1->chunk_work->data,
                                                   from_h1->chunk_work->data_len));
                        return APR_EINVAL;
                    }
                    /* valid chunk size 0, indicates end of body */
                    if (*pconsumed != len) {
                        ap_log_perror(APLOG_MARK, APLOG_WARNING, 0, from_h1->pool,
                                      "h2_from_h1(%d): end chunk read, but %ld bytes remain",
                                      from_h1->stream_id, (long)len - *pconsumed);
                    }
                    set_state(from_h1, H2_RESP_ST_DONE);
                }
                h2_bucket_reset(from_h1->chunk_work);
                ap_log_perror(APLOG_MARK, APLOG_TRACE2, 0, from_h1->pool,
                              "h2_from_h1(%d): read chunk size %ld",
                              from_h1->stream_id, (long)from_h1->remain_len);
                break;
            }
        }
    }
    
    if (h2_bucket_available(from_h1->chunk_work) == 0) {
        /* our chunk_work buffer is full, yet we have not seen a
         * line end. Report an error. */
        ap_log_perror(APLOG_MARK, APLOG_ERR, APR_EINVAL, from_h1->pool,
                      "h2_from_h1(%d): garbled chunk size: %s",
                      from_h1->stream_id,
                      apr_pstrndup(from_h1->pool, from_h1->chunk_work->data,
                                   from_h1->chunk_work->data_len));
        return APR_EINVAL;
    }
    
    *pconsumed = copied;
    return APR_SUCCESS;
}

static apr_status_t copy_body(h2_from_h1 *from_h1,
                              h2_bucket *bucket,
                              const char *data, apr_size_t len,
                              apr_size_t *pconsumed) {
    if (from_h1->chunked) {
        if (from_h1->remain_len > 0) {
            /* inside a chunk, copy out as much as we can */
            if (len > from_h1->remain_len) {
                len = from_h1->remain_len;
            }
            *pconsumed = h2_bucket_append(bucket, data, len);
            from_h1->remain_len -= *pconsumed;
        }
        
        if (from_h1->remain_len == 0) {
            apr_status_t status = read_chunk_size(from_h1, data, len, pconsumed);
            if (status != APR_SUCCESS) {
                return status;
            }
            
            if (from_h1->state == H2_RESP_ST_DONE) {
                /* that was the last, empty chunk that ended the body */
                return APR_SUCCESS;
            }
            
            if (from_h1->remain_len > 0) {
                /* we got a valid chunk size, copy the bytes */
                data += *pconsumed;
                len -= *pconsumed;
                if (len > 0) {
                    apr_size_t copied = 0;
                    status = copy_body(from_h1, bucket, data, len, &copied);
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
        if (len > from_h1->remain_len) {
            /* body is longer then declared in headers */
            ap_log_perror(APLOG_MARK, APLOG_ERR, APR_EINVAL, from_h1->pool,
                          "h2_from_h1(%d): body/chunk len %ld exceeded by %ld",
                          from_h1->stream_id, (long)from_h1->remain_len,
                          (long)(from_h1->remain_len - len));
            return APR_EINVAL;
        }
        *pconsumed = h2_bucket_append(bucket, data, len);
        from_h1->remain_len -= *pconsumed;
        if (from_h1->remain_len == 0) {
            set_state(from_h1, H2_RESP_ST_DONE);
        }
    }
    return APR_SUCCESS;
}


apr_status_t h2_from_h1_http_convert(h2_from_h1 *from_h1, h2_bucket *bucket,
                                      const char *data, apr_size_t len,
                                      apr_size_t *pconsumed)
{
    apr_status_t status = APR_SUCCESS;
    *pconsumed = 0;
    
    if (len > 0) {
        switch (from_h1->state) {
            case H2_RESP_ST_STATUS_LINE:
            case H2_RESP_ST_HEADERS:
                status = ensure_buffer(from_h1);
                if (status != APR_SUCCESS) {
                    return status;
                }
                
                *pconsumed = h2_bucket_append(from_h1->rawhead, data, len);
                if (*pconsumed > 0) {
                    if (from_h1->state == H2_RESP_ST_STATUS_LINE) {
                        /* Need to parse a valid HTTP/1.1 status line here */
                        status = parse_status_line(from_h1);
                    }
                    
                    if (from_h1->state == H2_RESP_ST_HEADERS) {
                        status = parse_headers(from_h1);
                    }
                    
                    if (from_h1->state == H2_RESP_ST_BODY
                        && from_h1->rawhead
                        && from_h1->offset < from_h1->rawhead->data_len) {
                        /* these bytes belong to the body */
                        long left = from_h1->rawhead->data_len - from_h1->offset;
                        if (left > *pconsumed) {
                            ap_log_perror(APLOG_MARK, APLOG_ERR, 0, from_h1->pool,
                                          "h2_from_h1(%d): headers parsed, but"
                                          " more bytes left (%ld) than "
                                          "we consumed (%ld)",
                                          from_h1->stream_id, left, *pconsumed);
                        }
                        else {
                            *pconsumed -= left;
                        }
                    }
                }
                break;
                
            case H2_RESP_ST_BODY:
                status = copy_body(from_h1, bucket, data, len, pconsumed);
                if (from_h1->state == H2_RESP_ST_DONE) {
                    ap_log_perror(APLOG_MARK, APLOG_TRACE2, 0, from_h1->pool,
                                  "h2_from_h1(%d): body done",
                                  from_h1->stream_id);
                }
                break;
                
            case H2_RESP_ST_DONE:
                /* We get content after we were done, something is not
                 * right here */
                ap_log_perror(APLOG_MARK, APLOG_ERR, APR_EINVAL, from_h1->pool,
                              "h2_from_h1(%d): body done, but receiving %ld more bytes",
                              from_h1->stream_id, (long)len);
                status = APR_EINVAL;
                break;
                
            default:
                /* ??? */
                break;
        }
    }
    return status;
}
