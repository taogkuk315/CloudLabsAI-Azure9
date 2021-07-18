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

struct h2_workers {
    server_rec *s;
    apr_pool_t *pool;
    int aborted;
    
    int next_worker_id;
    int min_size;
    int max_size;
    
    apr_threadattr_t *thread_attr;
    
    struct h2_queue *workers;
    struct h2_queue *tasks_scheduled;
    
    int idle_worker_count;
    int max_idle_secs;
    
    struct apr_thread_mutex_t *lock;
    struct apr_thread_cond_t *task_added;
    struct apr_thread_cond_t *task_done;
};


static apr_status_t get_task_next(h2_worker *worker, h2_task **ptask, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        h2_task *task = NULL;
        status = APR_EOF;
        ++workers->idle_worker_count;
        while (!h2_worker_is_aborted(worker) && !workers->aborted) {
            h2_task *task = h2_queue_pop(workers->tasks_scheduled);
            if (task) {
                *ptask = task;
                status = APR_SUCCESS;
                ap_log_error(APLOG_MARK, APLOG_DEBUG, status, workers->s,
                             "h2_worker(%d): start task(%ld-%d)",
                             h2_worker_get_id(worker),
                             h2_task_get_session_id(task),
                             h2_task_get_stream_id(task));
                break;
            }
            
            /* Need to wait for either a new task to arrive our, if we
             * are not at the minimum workers count, wait our max idle
             * time until we reduce the workers */
            if (h2_queue_size(workers->workers) > workers->min_size) {
                apr_time_t max_wait = apr_time_from_sec(workers->max_idle_secs);
                status = apr_thread_cond_timedwait(workers->task_added,
                                                   workers->lock, max_wait);
                if (status == APR_TIMEUP) {
                    /* waited long enough */
                    if (h2_queue_size(workers->workers) > workers->min_size) {
                        ap_log_error(APLOG_MARK, APLOG_TRACE2, status, workers->s,
                                     "h2_workers: aborting idle worker");
                        h2_worker_abort(worker);
                        break;
                    }
                }
            }
            else {
                apr_thread_cond_wait(workers->task_added, workers->lock);
            }
        }
        --workers->idle_worker_count;
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
                     "h2_worker(%d): task(%ld-%d) done",
                     h2_worker_get_id(worker),
                     h2_task_get_session_id(task),
                     h2_task_get_stream_id(task));
        
        
        h2_task_destroy(task);
        
        apr_thread_cond_signal(workers->task_done);
        apr_thread_mutex_unlock(workers->lock);
    }
}

static void worker_done(h2_worker *worker, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                     "h2_worker(%d): done", h2_worker_get_id(worker));
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
                 "h2_workers: adding worker(%d)", h2_worker_get_id(w));
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

h2_workers *h2_workers_create(server_rec *s, apr_pool_t *pool,
                              int min_size, int max_size)
{
    assert(s);
    assert(pool);
    apr_status_t status = APR_SUCCESS;

    h2_workers *workers = apr_pcalloc(pool, sizeof(h2_workers));
    if (workers) {
        workers->s = s;
        workers->pool = pool;
        workers->min_size = min_size;
        workers->max_size = max_size;
        workers->max_idle_secs = 10;
        
        apr_threadattr_create(&workers->thread_attr, workers->pool);
        
        workers->workers = h2_queue_create(workers->pool, NULL);
        workers->tasks_scheduled = h2_queue_create(workers->pool, NULL);
        
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
    if (workers->tasks_scheduled) {
        h2_queue_destroy(workers->tasks_scheduled);
        workers->tasks_scheduled = NULL;
    }
    if (workers->workers) {
        h2_queue_destroy(workers->workers);
        workers->workers = NULL;
    }
}

apr_status_t h2_workers_schedule(h2_workers *workers, h2_task *task)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, status, workers->s,
                     "h2_workers: scheduling task(%ld-%d)",
                     h2_task_get_session_id(task),
                     h2_task_get_stream_id(task));
        if (workers->idle_worker_count <= 0
            && h2_queue_size(workers->workers) < workers->max_size) {
            ap_log_error(APLOG_MARK, APLOG_TRACE2, status, workers->s,
                         "h2_workers: adding worker");
            add_worker(workers);
        }
        h2_queue_append(workers->tasks_scheduled, task);
        
        apr_thread_cond_signal(workers->task_added);
        apr_thread_mutex_unlock(workers->lock);
    }
    return status;
}

typedef struct {
    long session_id;
    int n;
} stream_id_t;

static void *match_stream_id(void *ctx, int i, void *entry)
{
    stream_id_t *id = (stream_id_t*)ctx;
    if ((h2_task_get_session_id((h2_task*)entry) == id->session_id)
        && (h2_task_get_stream_id((h2_task*)entry) == id->n)) {
        return entry;
    }
    return NULL;
}

apr_status_t h2_workers_unschedule(h2_workers *workers,
                                   long session_id, int stream_id)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_TRACE1, 0, workers->s,
                     "h2_workers: join stream(%ld-%d) started",
                     session_id, stream_id);
        stream_id_t id = { session_id, stream_id };
        /* get the task, if it is still awaiting execution */
        h2_task *task = h2_queue_pop_find(workers->tasks_scheduled,
                                          match_stream_id, &id);
        if (task) {
            h2_task_abort(task);
            h2_task_destroy(task);
        }
        apr_thread_mutex_unlock(workers->lock);
    }
    return status;
}

static int abort_task(void *ctx, int id, void *entry, int index)
{
    h2_task_abort((h2_task*)entry);
    return 1;
}

static void *match_session_id(void *ctx, int id, void *entry)
{
    long *psession_id = (long *)ctx;
    if (h2_task_get_session_id((h2_task*)entry) == *psession_id) {
        return entry;
    }
    return NULL;
}

apr_status_t h2_workers_shutdown(h2_workers *workers, long session_id)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, workers->s,
                     "h2_workers: shutdown session(%ld) started", session_id);
        /* remove all tasks still pending for the given session */
        while (1) {
            h2_task *task = h2_queue_pop_find(workers->tasks_scheduled,
                                              match_session_id, &session_id);
            if (!task) {
                break;
            }
            h2_task_abort(task);
            h2_task_destroy(task);
        }
        apr_thread_mutex_unlock(workers->lock);
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, workers->s,
                     "h2_workers: shutdown session(%ld) done", session_id);
    }
    return status;
}

void h2_workers_log_stats(h2_workers *workers)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, workers->s,
                     "h2_workers: %ld threads, %ld tasks todo",
                     h2_queue_size(workers->workers),
                     h2_queue_size(workers->tasks_scheduled));
        apr_thread_mutex_unlock(workers->lock);
    }
}
