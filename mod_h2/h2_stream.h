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


#ifndef __mod_h2__h2_stream__
#define __mod_h2__h2_stream__

#include "h2_bucket.h"
#include "h2_bucket_queue.h"

#define H2_STREAM_ST_IDLE           0
#define H2_STREAM_ST_OPEN           1
#define H2_STREAM_ST_RESV_LOCAL     2
#define H2_STREAM_ST_RESV_REMOTE    3
#define H2_STREAM_ST_CLOSED_INPUT   4
#define H2_STREAM_ST_CLOSED_OUTPUT  5
#define H2_STREAM_ST_CLOSED         6


typedef struct h2_stream {
    int id;                  /* http2 stream id */
    int state;               /* stream state */
    int eoh;                 /* end of headers seen */

    conn_rec *c;             /* httpd connection for this */
    h2_bucket_queue *input;  /* http/1.1 input data */
    
    /* pseudo header values, see ch. 8.1.2.3 */
    const char *method;
    const char *path;
    const char *authority;
    const char *scheme;
    
    h2_bucket *work;
    
} h2_stream;

apr_status_t h2_stream_create(h2_stream **stream, int id, int state,
                              conn_rec *master,
                              h2_bucket_queue *input);

apr_status_t h2_stream_destroy(h2_stream *stream);

apr_status_t h2_stream_process(h2_stream *stream);

apr_status_t h2_stream_close_input(h2_stream *stream);
apr_status_t h2_stream_close_output(h2_stream *stream);

apr_status_t h2_stream_push(h2_stream *stream);

apr_status_t h2_stream_add_header(h2_stream *stream,
                                  const char *name, size_t nlen,
                                  const char *value, size_t vlen);

apr_status_t h2_stream_add_data(h2_stream *stream,
                                const char *data, size_t len);

apr_status_t h2_stream_end_headers(h2_stream *stream);

#endif /* defined(__mod_h2__h2_stream__) */
