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
#ifndef __mod_h2__h2_streams__
#define __mod_h2__h2_streams__

#include "h2_data_queue.h"

typedef struct h2_streams {
    conn_rec *c;
    apr_size_t max;
    struct h2_stream **streams;
} h2_streams;

apr_status_t h2_streams_init(h2_streams *streams, int max_streams,
                             conn_rec *c);

apr_status_t h2_streams_stream_create(h2_streams *streams,
                                      struct h2_stream **stream,
                                      int stream_id,
                                      h2_data_queue *request_data);

apr_status_t h2_streams_stream_destroy(h2_streams *streams, int stream_id);

struct h2_stream *h2_streams_get(h2_streams *streams, int stream_id);

#endif /* defined(__mod_h2__h2_streams__) */
