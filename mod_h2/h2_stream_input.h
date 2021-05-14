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

#ifndef __mod_h2__h2_stream_input__
#define __mod_h2__h2_stream_input__

#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>

typedef struct {
    char *buffer;
    apr_size_t length;
    apr_size_t start;
    apr_size_t end;
    int eos;
    int aborted;
    
    apr_thread_mutex_t *lock;
    apr_thread_cond_t *has_data;
    apr_thread_cond_t *has_space;
} h2_stream_input;


apr_status_t h2_stream_input_init(h2_stream_input *input, apr_pool_t *pool,
                                  apr_size_t bufsize);
apr_status_t h2_stream_input_destroy(h2_stream_input *input);

apr_status_t h2_stream_input_read(ap_filter_t *filter,
                                  apr_bucket_brigade *brigade,
                                  ap_input_mode_t mode,
                                  apr_read_type_e block,
                                  apr_off_t readbytes);


apr_status_t h2_stream_input_push(h2_stream_input *input,
                                  const char *data, apr_size_t len);

apr_status_t h2_stream_input_close(h2_stream_input *input);

#endif /* defined(__mod_h2__h2_stream_input__) */
