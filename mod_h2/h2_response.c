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

#include <nghttp2/nghttp2.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_util.h"
#include "h2_response.h"

h2_response *h2_response_create(int stream_id,
                                  apr_status_t task_status,
                                  const char *http_status,
                                  apr_array_header_t *hlines,
                                  apr_pool_t *pool)
{
    apr_size_t nvmax = 1 + (hlines? hlines->nelts : 0);
    /* we allocate one block for the h2_response and the array of
     * nghtt2_nv structures.
     */
    h2_response *response = apr_pcalloc(pool, sizeof(h2_response));
    if (response == NULL) {
        return NULL;
    }

    response->stream_id = stream_id;
    response->task_status = task_status;
    response->http_status = http_status;
    response->content_length = -1;
    response->headers = apr_table_make(pool, hlines->nelts);

    if (hlines) {
        int seen_clen = 0;
        for (int i = 0; i < hlines->nelts; ++i) {
            char *hline = ((char **)hlines->elts)[i];
            char *sep = strchr(hline, ':');
            if (!sep) {
                ap_log_perror(APLOG_MARK, APLOG_WARNING, APR_EINVAL, pool,
                              "h2_response(%d): invalid header[%d] '%s'",
                              response->stream_id, i, (char*)hline);
                /* not valid format, abort */
                return NULL;
            }
            (*sep++) = '\0';
            while (*sep == ' ' || *sep == '\t') {
                ++sep;
            }
            if (H2_HD_MATCH_LIT_CS("connection", hline)
                || H2_HD_MATCH_LIT_CS("proxy-connection", hline)
                || H2_HD_MATCH_LIT_CS("upgrade", hline)
                || H2_HD_MATCH_LIT_CS("keep-alive", hline)
                || H2_HD_MATCH_LIT_CS("transfer-encoding", hline)) {
                /* never forward, ch. 8.1.2.2 */
            }
            else {
                apr_table_merge(response->headers, hline, sep);
                if (*sep && H2_HD_MATCH_LIT_CS("content-length", hline)) {
                    char *end;
                    response->content_length = apr_strtoi64(sep, &end, 10);
                    if (sep == end) {
                        ap_log_perror(APLOG_MARK, APLOG_WARNING, APR_EINVAL, 
                                      pool,"h2_response(%d): content-length"
                                      " value not parsed: %s", 
                                      response->stream_id, sep);
                        response->content_length = -1;
                    }
                }
            }
        }

    }
    return response;
}

void h2_response_destroy(h2_response *resp)
{
}

h2_response *h2_response_clone(apr_pool_t *p, h2_response *resp)
{
    h2_response *n = apr_palloc(p, sizeof(*resp));
    *n = *resp;
    n->http_status = apr_pstrdup(p, resp->http_status);
    n->headers = apr_table_clone(p, resp->headers);
    return n;
}


