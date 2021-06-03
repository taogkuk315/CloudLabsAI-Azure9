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

#ifndef __mod_h2__h2_worker__
#define __mod_h2__h2_worker__

struct h2_task;
struct h2_worker;

typedef apr_status_t h2_worker_task_next_fn(struct h2_worker *worker,
                                            struct h2_task **ptask,
                                            void *ctx);

typedef void h2_worker_task_done_fn(struct h2_worker *worker,
                                    struct h2_task *ptask,
                                    apr_status_t status,
                                    void *ctx);

typedef void h2_worker_done_fn(struct h2_worker *worker, void *ctx);

typedef struct h2_worker {
    int id;
    apr_thread_t *thread;
    h2_worker_task_next_fn *get_next;
    h2_worker_task_done_fn *task_done;
    h2_worker_done_fn *worker_done;
    void *ctx;
    int aborted;
    
    struct h2_task *current;
} h2_worker;

h2_worker *h2_worker_create(int id,
                            apr_pool_t *pool,
                            apr_threadattr_t *attr,
                            h2_worker_task_next_fn *get_next,
                            h2_worker_task_done_fn *task_done,
                            h2_worker_done_fn *worker_done,
                            void *ctx);

apr_status_t h2_worker_destroy(h2_worker *worker);

int h2_worker_is_aborted(h2_worker *worker);

#endif /* defined(__mod_h2__h2_worker__) */
