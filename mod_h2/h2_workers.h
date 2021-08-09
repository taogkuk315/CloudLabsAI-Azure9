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

#ifndef __mod_h2__h2_workers__
#define __mod_h2__h2_workers__

/* Thread pool specific to executing h2_tasks. Has a minimum and maximum 
 * number of workers it creates. Starts with minimum workers and adds
 * some on load, reduces the number again when idle.
 *
 */
struct apr_thread_mutex_t;
struct apr_thread_cond_t;
struct h2_task;

typedef struct h2_workers h2_workers;

/* Create a worker pool with the given minimum and maximum number of
 * threads.
 */
h2_workers *h2_workers_create(server_rec *s, apr_pool_t *pool,
                              int min_size, int max_size);

/* Destroy the worker pool and all its threads. 
 */
void h2_workers_destroy(h2_workers *workers);

/* Schedule a task for execution.
 */
apr_status_t h2_workers_schedule(h2_workers *workers, h2_task *task);

/* If the task is scheduled, but not been started yet, will remove it from 
 * the schedule and return APR_SUCCESS.
 * If the task is running and wait != 0, will wait for the task to 
 * complete. Returns APR_SUCCESS when done.
 * If the task is running and wait == 0, will return immediately 
 * with APR_EAGAIN.
 */
apr_status_t h2_workers_join(h2_workers *workers, h2_task *task, int wait);

/* Shutdown all activities connection to the session.
 */
apr_status_t h2_workers_shutdown(h2_workers *workers,
                                 long session_id);

/* Log some statistics about budy/idle workers etc. 
 */
void h2_workers_log_stats(h2_workers *workers);

#endif /* defined(__mod_h2__h2_workers__) */
