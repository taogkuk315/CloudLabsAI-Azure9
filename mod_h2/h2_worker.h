/* Copyright 2015 greenbytes GmbH (https://www.greenbytes.de)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __mod_h2__h2_worker__
#define __mod_h2__h2_worker__

struct apr_thread_cond_t;
struct h2_task;

/* h2_worker is a basically a apr_thread_t that reads fromt he h2_workers
 * task queue and runs h2_tasks it is given.
 */
typedef struct h2_worker h2_worker;

/* Invoked when the worker wants a new task. Will block
 * until a task becomes available or the worker itself
 * gets aborted (idle timeout, for example). */
typedef apr_status_t h2_worker_task_next_fn(h2_worker *worker,
                                            h2_task **ptask,
                                            void *ctx);

/* Invoked when the worker has finished a task. May return the 
 * next task to work on or NULL. Will not block. */
typedef h2_task *h2_worker_task_done_fn(h2_worker *worker,
                                        h2_task *ptask,
                                        apr_status_t status,
                                        void *ctx);

/* Invoked just before the worker thread exits. */
typedef void h2_worker_done_fn(h2_worker *worker, void *ctx);

/* Create a new worker with given id, pool and attributes, callbacks
 * callback parameter.
 */
h2_worker *h2_worker_create(int id,
                            apr_pool_t *pool,
                            apr_threadattr_t *attr,
                            h2_worker_task_next_fn *get_next,
                            h2_worker_task_done_fn *task_done,
                            h2_worker_done_fn *worker_done,
                            void *ctx);

apr_status_t h2_worker_destroy(h2_worker *worker);

void h2_worker_abort(h2_worker *worker);

int h2_worker_get_id(h2_worker *worker);

int h2_worker_is_aborted(h2_worker *worker);

apr_pool_t *h2_worker_get_pool(h2_worker *worker);

apr_bucket_alloc_t *h2_worker_get_bucket_alloc(h2_worker *worker);

apr_socket_t *h2_worker_get_socket(h2_worker *worker);

apr_thread_t *h2_worker_get_thread(h2_worker *worker);

struct apr_thread_cond_t *h2_worker_get_cond(h2_worker *worker);

struct h2_task *h2_worker_get_task(h2_worker *worker);


#endif /* defined(__mod_h2__h2_worker__) */
