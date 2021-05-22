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

#ifndef __mod_h2__h2_stream_output__
#define __mod_h2__h2_stream_output__

#include "h2_bucket_queue.h"
#include "h2_stream.h"

typedef apr_status_t (*h2_output_converter)(h2_bucket *bucket,
                                            void *conv_data,
                                            const char *data, apr_size_t len,
                                            apr_size_t *pconsumed);

typedef struct h2_stream_output {
    h2_bucket_queue *queue;
    int stream_id;
    int eos;
    int aborted;
    h2_bucket *cur;
    apr_size_t cur_offset;
    
    h2_output_converter conv;
    void *conv_ctx;
} h2_stream_output;

h2_stream_output *h2_stream_output_create(apr_pool_t *pool,
                                          int stream_id,
                                          h2_bucket_queue *q);

void h2_stream_output_destroy(h2_stream_output *output);

apr_status_t h2_stream_output_write(h2_stream_output *output,
                                    ap_filter_t* filter,
                                    apr_bucket_brigade* brigade);

void h2_stream_output_set_converter(h2_stream_output *output,
                                    h2_output_converter conv,
                                    void *conv_ctx);

#endif /* defined(__mod_h2__h2_stream_output__) */
