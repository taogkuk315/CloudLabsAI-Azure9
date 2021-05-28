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

#ifndef __mod_h2__h2_task__
#define __mod_h2__h2_task__

struct h2_task;

typedef enum {
    H2_TASK_ST_IDLE,
    H2_TASK_ST_STARTED,
    H2_TASK_ST_READY,
    H2_TASK_ST_DONE
} h2_task_state_t;

typedef void h2_task_state_change_cb(struct h2_task *task,
                                     h2_task_state_t,
                                     void *cb_ctx);

typedef struct h2_task {
    conn_rec *c;
    int stream_id;
    h2_task_state_t state;
    int aborted;
    int auto_destroy;
    
    struct h2_task_input *input;    /* http/1.1 input data */
    struct h2_task_output *output;  /* response body data */
    struct h2_response *response;     /* response meta data */
    
} h2_task;

h2_task *h2_task_create(int stream_id,
                        conn_rec *master,
                        struct h2_bucket_queue *input,
                        struct h2_bucket_queue *output);

apr_status_t h2_task_destroy(h2_task *task);

apr_status_t h2_task_do(h2_task *task);

void h2_task_abort(h2_task *task);
int h2_task_is_aborted(h2_task *task);
int h2_task_is_busy(h2_task *task);
int h2_task_is_done(h2_task *task);

void h2_task_hooks_init(void);
int h2_task_pre_conn(h2_task *task, conn_rec *c);

void h2_task_set_auto_destroy(h2_task *task, int auto_destroy);

#endif /* defined(__mod_h2__h2_task__) */
