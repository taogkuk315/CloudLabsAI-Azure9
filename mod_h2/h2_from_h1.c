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
#include "h2_bucket.h"
#include "h2_response.h"
#include "h2_from_h1.h"
#include "h2_task.h"
#include "h2_task_output.h"
#include "h2_util.h"

struct h2_from_h1 {
    int stream_id;
    h2_from_h1_state_t state;
    apr_pool_t *pool;
    apr_bucket_brigade *bb;
    apr_bucket_brigade *tmp;
    
    apr_size_t content_length;
    int chunked;
    
    const char *status;
    apr_array_header_t *hlines;
    
    struct h2_response *head;
};

static void set_state(h2_from_h1 *from_h1, h2_from_h1_state_t state);

h2_from_h1 *h2_from_h1_create(int stream_id, apr_pool_t *pool, 
                              apr_bucket_alloc_t *bucket_alloc)
{
    h2_from_h1 *from_h1 = apr_pcalloc(pool, sizeof(h2_from_h1));
    if (from_h1) {
        from_h1->stream_id = stream_id;
        from_h1->pool = pool;
        from_h1->tmp = apr_brigade_create(pool, bucket_alloc);
        from_h1->state = H2_RESP_ST_STATUS_LINE;
        from_h1->hlines = apr_array_make(pool, 10, sizeof(char *));
    }
    return from_h1;
}

apr_status_t h2_from_h1_destroy(h2_from_h1 *from_h1)
{
    if (from_h1->head) {
        h2_response_destroy(from_h1->head);
        from_h1->head = NULL;
    }
    from_h1->tmp = NULL;
    from_h1->bb = NULL;
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
    }
}

h2_response *h2_from_h1_get_response(h2_from_h1 *from_h1)
{
    h2_response *head = from_h1->head;
    from_h1->head = NULL;
    return head;
}

static apr_status_t make_h2_headers(h2_from_h1 *from_h1, request_rec *r)
{
    from_h1->head = h2_response_create(from_h1->stream_id, APR_SUCCESS,
                                       from_h1->status, from_h1->hlines,
                                       from_h1->pool);
    if (from_h1->head == NULL) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_EINVAL, r->connection,
                      "h2_from_h1(%d): unable to create resp_head",
                      from_h1->stream_id);
        return APR_EINVAL;
    }
    from_h1->content_length = from_h1->head->content_length;
    from_h1->chunked = r->chunked;
    
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, r->connection,
                  "h2_from_h1(%d): converted %d headers, content-length: %d"
                  ", chunked=%d",
                  from_h1->stream_id, (int)from_h1->head->nvlen,
                  (int)from_h1->content_length, (int)from_h1->chunked);
    
    set_state(from_h1, ((from_h1->chunked || from_h1->content_length > 0)?
                        H2_RESP_ST_BODY : H2_RESP_ST_DONE));
    /* We are ready to be sent to the client */
    return APR_SUCCESS;
}

static apr_status_t parse_header(h2_from_h1 *from_h1, ap_filter_t* f, 
                                 char *line) {
    if (line[0] == ' ' || line[0] == '\t') {
        /* continuation line from the header before this */
        while (line[0] == ' ' || line[0] == '\t') {
            ++line;
        }
        
        char **plast = apr_array_pop(from_h1->hlines);
        if (plast == NULL) {
            /* not well formed */
            return APR_EINVAL;
        }
        APR_ARRAY_PUSH(from_h1->hlines, const char*) = apr_psprintf(from_h1->pool, "%s %s", *plast, line);
    }
    else {
        /* new header line */
        APR_ARRAY_PUSH(from_h1->hlines, const char*) = apr_pstrdup(from_h1->pool, line);
    }
    return APR_SUCCESS;
}

static apr_status_t get_line(h2_from_h1 *from_h1, apr_bucket_brigade *bb,
                             ap_filter_t* f, char *line, apr_size_t len)
{
    if (!from_h1->bb) {
        from_h1->bb = apr_brigade_create(from_h1->pool, f->c->bucket_alloc);
    }
    else {
        apr_brigade_cleanup(from_h1->bb);                
    }
    apr_status_t status = apr_brigade_split_line(from_h1->bb, bb, 
                                                 APR_BLOCK_READ, 
                                                 HUGE_STRING_LEN);
    if (status == APR_SUCCESS) {
        --len;
        status = apr_brigade_flatten(from_h1->bb, line, &len);
        if (status == APR_SUCCESS) {
            /* we assume a non-0 containing line and remove
             * trailing crlf. */
            line[len] = '\0';
            if (len >= 2 && !strcmp(H2_CRLF, line + len - 2)) {
                len -= 2;
                line[len] = '\0';
            }
            
            apr_brigade_cleanup(from_h1->bb);
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, f->c,
                          "h2_from_h1(%d): read line: %s",
                          from_h1->stream_id, line);
        }
    }
    return status;
}

apr_status_t h2_from_h1_read_response(h2_from_h1 *from_h1, ap_filter_t* f,
                                      apr_bucket_brigade* bb)
{
    apr_status_t status = APR_SUCCESS;
    char line[HUGE_STRING_LEN];
    
    if ((from_h1->state == H2_RESP_ST_BODY) 
        || (from_h1->state == H2_RESP_ST_DONE)) {
        if (from_h1->chunked) {
            /* The httpd core HTTP_HEADER filter has or will install the 
             * "CHUNK" output transcode filter, which appears further down 
             * the filter chain. We do not want it for HTTP/2.
             * Once we successfully deinstalled it, this filter has no
             * further function and we remove it.
             */
            status = ap_remove_output_filter_byhandle(f->r->output_filters, 
                                                      "CHUNK");
            if (status == APR_SUCCESS) {
                ap_remove_output_filter(f);
            }
        }
        
        return ap_pass_brigade(f->next, bb);
    }
    
    while (!APR_BRIGADE_EMPTY(bb) && status == APR_SUCCESS) {
        
        switch (from_h1->state) {
                
            case H2_RESP_ST_STATUS_LINE:
            case H2_RESP_ST_HEADERS:
                status = get_line(from_h1, bb, f, line, sizeof(line));
                if (status != APR_SUCCESS) {
                    return status;
                }
                if (from_h1->state == H2_RESP_ST_STATUS_LINE) {
                    /* instead of parsing, just take it directly */
                    from_h1->status = apr_psprintf(from_h1->pool, 
                                                   "%d", f->r->status);
                    from_h1->state = H2_RESP_ST_HEADERS;
                }
                else if (line[0] == '\0') {
                    /* end of headers, create the h2_response and
                     * pass the rest of the brigade down the filter
                     * chain.
                     */
                    status = make_h2_headers(from_h1, f->r);
                    if (from_h1->bb) {
                        apr_brigade_destroy(from_h1->bb);
                        from_h1->bb = NULL;
                    }
                    if (!APR_BRIGADE_EMPTY(bb)) {
                        return ap_pass_brigade(f->next, bb);
                    }
                }
                else {
                    status = parse_header(from_h1, f, line);
                }
                break;
                
            default:
                return ap_pass_brigade(f->next, bb);
        }
        
    }
    
    return status;
}

