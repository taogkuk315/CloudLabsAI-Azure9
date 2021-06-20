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

#ifndef __mod_h2__h2_workers__
#define __mod_h2__h2_workers__

struct apr_thread_mutex_t;
struct apr_thread_cond_t;
struct h2_task;

typedef struct h2_workers {
    server_rec *s;
    apr_pool_t *pool;
    int aborted;
    
    int next_worker_id;
    int min_size;
    int max_size;
    
    apr_threadattr_t *thread_attr;
    
    struct h2_queue *workers;
    struct h2_queue *tasks_todo;
    
    struct apr_thread_mutex_t *lock;
    struct apr_thread_cond_t *task_added;
    struct apr_thread_cond_t *task_done;
} h2_workers;


h2_workers *h2_workers_create(server_rec *s, apr_pool_t *pool,
                              int min_size, int max_size);

void h2_workers_destroy(h2_workers *workers);

apr_status_t h2_workers_schedule(h2_workers *workers, struct h2_task *task,
                                 int session_id);

apr_status_t h2_workers_shutdown(h2_workers *workers, int session_id);

#endif /* defined(__mod_h2__h2_workers__) */
