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

#include <assert.h>
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_queue.h"
#include "h2_task.h"
#include "h2_worker.h"
#include "h2_workers.h"

static void free_worker(void *w)
{
    // TODO
}

static void free_task(void *t)
{
    // TODO
}

static apr_status_t get_task_next(h2_worker *worker, h2_task **ptask, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        h2_task *task = NULL;
        status = APR_EOF;
        while (!h2_worker_is_aborted(worker) && !workers->aborted) {
            h2_task *task = h2_queue_pop(workers->tasks_todo);
            if (task) {
                *ptask = task;
                status = APR_SUCCESS;
                ap_log_error(APLOG_MARK, APLOG_DEBUG, status, workers->s,
                             "h2_worker(%d): get task(%d-%d)",
                             worker->id, task->session_id, task->stream_id);
                break;
            }
            apr_thread_cond_wait(workers->task_added, workers->lock);
        }
        
        apr_thread_mutex_unlock(workers->lock);
    }
    return status;
}

static void task_done(h2_worker *worker, h2_task *task,
                      apr_status_t task_status, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, status, workers->s,
                     "h2_worker(%d): task(%d-%d) done",
                     worker->id, task->session_id, task->stream_id);
        
        h2_queue_remove(workers->tasks_active, task);
        apr_thread_cond_broadcast(workers->task_done);
        h2_task_destroy(task, workers->pool);
        
        apr_thread_mutex_unlock(workers->lock);
    }
}

static void worker_done(h2_worker *worker, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                     "h2_worker(%d): done", worker->id);
        h2_queue_remove(workers->workers, worker);
        apr_thread_mutex_unlock(workers->lock);
    }
}


static apr_status_t add_worker(h2_workers *workers)
{
    h2_worker *w = h2_worker_create(workers->next_worker_id++,
                                    workers->pool, workers->thread_attr,
                                    get_task_next, task_done, worker_done,
                                    workers);
    if (!w) {
        return APR_ENOMEM;
    }
    ap_log_error(APLOG_MARK, APLOG_TRACE2, 0, workers->s,
                 "h2_workers: adding worker(%d)", w->id);
    return h2_queue_append(workers->workers, w);
}

static apr_status_t h2_workers_start(h2_workers *workers) {
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                      "h2_workers: starting");

        while (h2_queue_size(workers->workers) < workers->min_size
               && status == APR_SUCCESS) {
            status = add_worker(workers);
        }
        apr_thread_mutex_unlock(workers->lock);
    }
    return status;
}

void h2_workers_destroy(h2_workers *workers);

h2_workers *h2_workers_create(server_rec *s, apr_pool_t *pool,
                              int min_size, int max_size)
{
    assert(s);
    assert(pool);
    assert(min_size > 0);
    assert(max_size >= min_size);
    
    apr_status_t status = APR_SUCCESS;

    h2_workers *workers = apr_pcalloc(pool, sizeof(h2_workers));
    if (workers) {
        workers->s = s;
        workers->pool = pool;
        workers->min_size = min_size;
        workers->max_size = max_size;
        
        apr_threadattr_create(&workers->thread_attr, workers->pool);
        
        workers->workers = h2_queue_create(workers->pool, free_worker);
        workers->tasks_todo = h2_queue_create(workers->pool, free_task);
        workers->tasks_active = h2_queue_create(workers->pool, free_task);
        
        status = apr_thread_mutex_create(&workers->lock,
                                         APR_THREAD_MUTEX_DEFAULT,
                                         workers->pool);
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_create(&workers->task_added, workers->pool);
            if (status == APR_SUCCESS) {
                status = apr_thread_cond_create(&workers->task_done,
                                                workers->pool);
            }
        }
        
        if (status == APR_SUCCESS) {
            status = h2_workers_start(workers);
        }
        
        if (status != APR_SUCCESS) {
            h2_workers_destroy(workers);
            workers = NULL;
        }
    }
    return workers;
}

void h2_workers_destroy(h2_workers *workers)
{
    if (workers->task_done) {
        apr_thread_cond_destroy(workers->task_done);
        workers->task_done = NULL;
    }
    if (workers->task_added) {
        apr_thread_cond_destroy(workers->task_added);
        workers->task_added = NULL;
    }
    if (workers->lock) {
        apr_thread_mutex_destroy(workers->lock);
        workers->lock = NULL;
    }
    if (workers->tasks_todo) {
        h2_queue_destroy(workers->tasks_todo);
        workers->tasks_todo = NULL;
    }
    if (workers->tasks_active) {
        h2_queue_destroy(workers->tasks_active);
        workers->tasks_active = NULL;
    }
    if (workers->workers) {
        h2_queue_destroy(workers->workers);
        workers->workers = NULL;
    }
}

apr_status_t h2_workers_schedule(h2_workers *workers, h2_task *task,
                                 int session_id)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        h2_queue_append_id(workers->tasks_todo, session_id, task);
        apr_thread_cond_signal(workers->task_added);
        apr_thread_mutex_unlock(workers->lock);
    }
    return status;
}

static int abort_task(void *ctx, int id, void *entry, int index)
{
    h2_task_abort((h2_task*)entry);
    return 1;
}

apr_status_t h2_workers_shutdown(h2_workers *workers, int session_id)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, workers->s,
                     "h2_workers: shutdown session(%d) started",
                     session_id);
        /* remove all tasks still pending for the given owner */
        while (1) {
            h2_task *task = h2_queue_pop_id(workers->tasks_todo, session_id);
            if (!task) {
                break;
            }
            h2_task_abort(task);
            h2_task_destroy(task, workers->pool);
        }
        apr_thread_mutex_unlock(workers->lock);
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, workers->s,
                     "h2_workers: shutdown session(%d) done",
                     session_id);
    }
    return status;
}
