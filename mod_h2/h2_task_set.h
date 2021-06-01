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

#ifndef __mod_h2__h2_task_set__
#define __mod_h2__h2_task_set__

/**
 * A set of h2_task instances. Thread safe.
 *
 */

struct h2_queue;
struct apr_thread_mutex_t;

typedef h2_task *(*h2_task_set_match_fn)(void *ctx, h2_task *stream);

typedef struct h2_task_set {
    struct h2_queue *queue;
    struct apr_thread_mutex_t *lock;
} h2_task_set;

h2_task_set *h2_task_set_create(apr_pool_t *pool);

void h2_task_set_destroy(h2_task_set *sp);

apr_status_t h2_task_set_term(h2_task_set *sp);

apr_status_t h2_task_set_add(h2_task_set *sp, h2_task *stream);

h2_task *h2_task_set_get(h2_task_set *sp, int stream_id);

h2_task *h2_task_set_remove(h2_task_set *sp,h2_task *stream);

void h2_task_set_remove_all(h2_task_set *sp);

void h2_task_set_abort_all(h2_task_set *sp);
void h2_task_set_destroy_all(h2_task_set *sp);

int h2_task_set_is_empty(h2_task_set *sp);

h2_task *h2_task_set_find(h2_task_set *sp,
                              h2_task_set_match_fn match, void *ctx);

#endif /* defined(__mod_h2__h2_task_set__) */
