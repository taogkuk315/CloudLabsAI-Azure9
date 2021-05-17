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

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_frame.h"


apr_status_t h2_frame_to_http(const nghttp2_frame *frame,
                              const char **pdata,
                              apr_size_t *pdatalen)
{
    *pdata = NULL;
    *pdatalen = 0;
    
    if (frame->hd.type == NGHTTP2_HEADERS) {
        /* Count all header name/value pairs lengths, add 4 for each pair
         * for separator ': ' and CRLF.
         * There is possibly a final CRLF, plus we have to add ' HTTP/1.1' to
         * the status line.
         */
        apr_size_t len = 2 + 9;
        for (int i = 0; i < frame->headers.nvlen; ++i) {
            nghttp2_nv *nv = frame->headers.nva+i;
            len += nv->namelen + 2 + nv->valuelen + 2;
        }
        char *data = calloc(len, sizeof(char));
        if (data == NULL) {
            return APR_ENOMEM;
        }
    }
    else if (frame->hd.type == NGHTTP2_HEADERS) {
        /* Count all header name/value pairs lengths, add 4 for each pair
         * for separator ': ' and CRLF and a possible, final CRLF.
         */
        apr_size_t len = 2;
        for (int i = 0; i < frame->headers.nvlen; ++i) {
            nghttp2_nv *nv = frame->headers.nva+i;
            len += nv->namelen + 2 + nv->valuelen + 2;
        }
        char *data = calloc(len, sizeof(char));
        if (data == NULL) {
            return APR_ENOMEM;
        }
    }
    else if (frame->hd.type == NGHTTP2_DATA) {
        // TODO
    }
    else {
        /* ingored */
    }
    
    return APR_SUCCESS;
}
