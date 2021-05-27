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

#ifndef __mod_h2__h2_stream_task__
#define __mod_h2__h2_stream_task__

struct h2_stream_task;

typedef enum {
    H2_TASK_ST_IDLE,
    H2_TASK_ST_STARTED,
    H2_TASK_ST_READY,
    H2_TASK_ST_DONE
} h2_stream_task_state_t;

typedef void h2_stream_task_state_change_cb(struct h2_stream_task *task,
                                            h2_stream_task_state_t,
                                            void *cb_ctx);

typedef struct h2_stream_task {
    conn_rec *c;
    int stream_id;
    h2_stream_task_state_t state;
    
    struct h2_stream_input *input;    /* http/1.1 input data */
    struct h2_stream_output *output;  /* response body data */
    struct h2_response *response;     /* response meta data */

    h2_stream_task_state_change_cb *state_change_cb;
    void *state_change_ctx;
    
} h2_stream_task;

h2_stream_task *h2_stream_task_create(int stream_id,
                                      conn_rec *master,
                                      h2_bucket_queue *input,
                                      h2_bucket_queue *output);

apr_status_t h2_stream_task_destroy(h2_stream_task *task);

apr_status_t h2_stream_task_do(h2_stream_task *task);


void h2_stream_hooks_init(void);
int h2_stream_task_pre_conn(h2_stream_task *task, conn_rec *c);

void h2_stream_task_set_state_change_cb(h2_stream_task *task,
                                        h2_stream_task_state_change_cb cb,
                                        void *cb_ctx);

#endif /* defined(__mod_h2__h2_stream_task__) */
