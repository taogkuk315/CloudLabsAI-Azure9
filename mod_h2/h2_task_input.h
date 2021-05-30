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

#ifndef __mod_h2__h2_task_input__
#define __mod_h2__h2_task_input__

struct h2_bucket;
struct h2_bucket_queue;

typedef struct h2_task_input {
    struct h2_bucket_queue *queue;
    int stream_id;
    int eos;
    int aborted;
    struct h2_bucket *cur;
    apr_size_t cur_offset;
} h2_task_input;

h2_task_input *h2_task_input_create(apr_pool_t *pool,
                                        int stream_id,
                                        h2_bucket_queue *q);
void h2_task_input_destroy(h2_task_input *input);

apr_status_t h2_task_input_read(h2_task_input *input,
                                  ap_filter_t* filter,
                                  apr_bucket_brigade* brigade,
                                  ap_input_mode_t mode,
                                  apr_read_type_e block,
                                  apr_off_t readbytes);

void h2_task_input_abort(h2_task_input *input);

#endif /* defined(__mod_h2__h2_task_input__) */
