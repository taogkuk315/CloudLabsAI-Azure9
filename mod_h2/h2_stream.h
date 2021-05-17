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

#include "h2_data_queue.h"

#define H2_STREAM_ST_IDLE           0
#define H2_STREAM_ST_OPEN           1
#define H2_STREAM_ST_RESV_LOCAL     2
#define H2_STREAM_ST_RESV_REMOTE    3
#define H2_STREAM_ST_CLOSED_INPUT   4
#define H2_STREAM_ST_CLOSED_OUTPUT  5
#define H2_STREAM_ST_CLOSED         6


typedef struct h2_stream {
    int id;
    int state;
    int eoh;
    conn_rec *c;
    
    h2_data_queue *request_data;
} h2_stream;

apr_status_t h2_stream_create(h2_stream **stream, int id, int state,
                              conn_rec *master,
                              h2_data_queue *request_data);

apr_status_t h2_stream_destroy(h2_stream *stream);

apr_status_t h2_stream_process(h2_stream *stream);


apr_status_t h2_stream_push(h2_stream *stream, const char *data,
                            apr_size_t length);
apr_status_t h2_stream_pull(h2_stream *stream, const char *data,
                            apr_size_t length, int *eos);


#endif /* defined(__mod_h2__h2_stream__) */
