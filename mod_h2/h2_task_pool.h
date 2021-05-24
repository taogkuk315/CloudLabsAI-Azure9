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

#ifndef __mod_h2__h2_task_pool__
#define __mod_h2__h2_task_pool__

#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>

#include "h2_queue.h"

typedef struct h2_task_pool {
    h2_queue *queue;
    apr_thread_mutex_t *lock;
} h2_task_pool;

h2_task_pool *h2_task_pool_create(apr_pool_t *pool);

void h2_task_pool_destroy(h2_task_pool *sp);

apr_status_t h2_task_pool_term(h2_task_pool *sp);

apr_status_t h2_task_pool_add(h2_task_pool *sp, h2_stream_task *task);

h2_stream_task *h2_task_pool_get(h2_task_pool *sp, int stream_id);

h2_stream_task *h2_task_pool_get_any(h2_task_pool *sp);

h2_stream_task *h2_task_pool_remove(h2_task_pool *sp,h2_stream_task *task);

#endif /* defined(__mod_h2__h2_task_pool__) */

