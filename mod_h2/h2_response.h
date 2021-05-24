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

#ifndef __mod_h2__h2_response__
#define __mod_h2__h2_response__

#define H2_RESP_ST_STATUS_LINE    0
#define H2_RESP_ST_HEADERS        1
#define H2_RESP_ST_BODY           2
#define H2_RESP_ST_DONE           4

typedef struct h2_response {
    int stream_id;
    conn_rec *c;
    int state;
    
    int chunked;
    apr_size_t body_len;
    apr_size_t remain_len;
    h2_bucket *chunk_work;
    
    apr_size_t offset;
    h2_bucket *rawhead;
    
    char *status;
    apr_array_header_t *hlines;
    
    const nghttp2_nv *nv;
    apr_size_t nvlen;
} h2_response;

h2_response *h2_response_create(int stream_id, conn_rec *c);
apr_status_t h2_response_init(h2_response *response, int stream_id, conn_rec *c);
apr_status_t h2_response_destroy(h2_response *response);

apr_status_t h2_response_http_convert(h2_bucket *bucket,
                                      void *conv_ctrx,
                                      const char *data, apr_size_t len,
                                      apr_size_t *pconsumed);

int h2_response_is_ready(h2_response *resp);

#endif /* defined(__mod_h2__h2_response__) */
