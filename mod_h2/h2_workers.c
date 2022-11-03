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

#include <assert.h>
#include <apr_atomic.h>
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_task.h"
#include "h2_task_queue.h"
#include "h2_worker.h"
#include "h2_workers.h"

static h2_task* pop_next_task(h2_workers *workers, h2_worker *worker)
{
    /* Each task queue belongs to one http2 session. We perform round
     * robin scheduling among queues and serve the tasks in the queue
     * in the order they appear.
     * TODO: priority scheduling of tasks should adapt the order
     * in the individual queue. This lacks the prio information from
     * nghttp2 library for now.
     * 
     */
    if (!H2_TQ_LIST_EMPTY(&workers->queues)) {
        h2_task_queue *q = H2_TQ_LIST_FIRST(&workers->queues);
        if (q) {
            H2_TQ_REMOVE(q);
            h2_task *task = h2_tq_pop_first(q);
            if (task) {
                h2_task_set_started(task, h2_worker_get_cond(worker));
                if (!H2_TQ_EMPTY(q)) {
                    H2_TQ_LIST_INSERT_TAIL(&workers->queues, q);
                }
                return task;
            }
        }
    }
    return NULL;
}

static apr_status_t get_task_next(h2_worker *worker, h2_task **ptask, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    *ptask = NULL;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        status = APR_EOF;
        apr_time_t max_wait = apr_time_from_sec(apr_atomic_read32(&workers->max_idle_secs));
        apr_time_t start_wait = apr_time_now();
        
        ++workers->idle_worker_count;
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                     "h2_worker(%d): looking for work", h2_worker_get_id(worker));
        while (!h2_worker_is_aborted(worker) && !workers->aborted) {
            h2_task *task = pop_next_task(workers, worker);
            if (task) {
                *ptask = task;
                status = APR_SUCCESS;
                ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                             "h2_worker(%d): start task(%s)",
                             h2_worker_get_id(worker), h2_task_get_id(task));
                break;
            }
            
            /* Need to wait for either a new task to arrive or, if we
             * are not at the minimum workers count, wait our max idle
             * time until we reduce the number of workers */
            if (workers->worker_count > workers->min_size) {
                apr_time_t now = apr_time_now();
                if (now >= (start_wait + max_wait)) {
                    /* waited long enough without getting a task. */
                    status = APR_TIMEUP;
                }
                else {
                    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                                 "h2_worker(%d): waiting signal, worker_count=%d",
                                 h2_worker_get_id(worker), (int)workers->worker_count);
                    status = apr_thread_cond_timedwait(workers->task_added,
                                                       workers->lock, max_wait);
                }
                if (status == APR_TIMEUP) {
                    /* waited long enough */
                    if (workers->worker_count > workers->min_size) {
                        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                                     "h2_workers: aborting idle worker");
                        h2_worker_abort(worker);
                        break;
                    }
                }
            }
            else {
                ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                             "h2_worker(%d): waiting signal (eternal), worker_count=%d",
                             h2_worker_get_id(worker), (int)workers->worker_count);
                apr_thread_cond_wait(workers->task_added, workers->lock);
            }
        }
        --workers->idle_worker_count;
        apr_thread_mutex_unlock(workers->lock);
    }
    return status;
}

static h2_task *task_done(h2_worker *worker, h2_task *task,
                          apr_status_t task_status, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    h2_task *next_task = NULL;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        h2_task_set_finished(task);
        next_task = pop_next_task(workers, worker);
        
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                     "h2_worker(%d): task(%s) done, next(%s)",
                     h2_worker_get_id(worker), h2_task_get_id(task),
                     next_task? h2_task_get_id(next_task) : "null");
        
        apr_thread_cond_signal(h2_worker_get_cond(worker));
        apr_thread_mutex_unlock(workers->lock);
    }
    return next_task;
}

static void worker_done(h2_worker *worker, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                     "h2_worker(%d): done", h2_worker_get_id(worker));
        H2_WORKER_REMOVE(worker);
        --workers->worker_count;
        h2_worker_destroy(worker);
        
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
    ++workers->worker_count;
    H2_WORKER_LIST_INSERT_TAIL(&workers->workers, w);
    return APR_SUCCESS;
}

static apr_status_t h2_workers_start(h2_workers *workers) {
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                      "h2_workers: starting");

        while (workers->worker_count < workers->min_size
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
        apr_atomic_set32(&workers->max_idle_secs, 10);
        
        apr_threadattr_create(&workers->thread_attr, workers->pool);
        
        APR_RING_INIT(&workers->workers, h2_worker, link);
        APR_RING_INIT(&workers->queues, h2_task_queue, link);
        
        status = apr_thread_mutex_create(&workers->lock,
                                         APR_THREAD_MUTEX_DEFAULT,
                                         workers->pool);
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_create(&workers->task_added, workers->pool);
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
    if (workers->task_added) {
        apr_thread_cond_destroy(workers->task_added);
        workers->task_added = NULL;
    }
    if (workers->lock) {
        apr_thread_mutex_destroy(workers->lock);
        workers->lock = NULL;
    }
    while (!H2_TQ_LIST_EMPTY(&workers->queues)) {
        h2_task_queue *q = H2_TQ_LIST_FIRST(&workers->queues);
        H2_TQ_REMOVE(q);
        h2_tq_destroy(q);
    }
    while (!H2_WORKER_LIST_EMPTY(&workers->workers)) {
        h2_worker *w = H2_WORKER_LIST_FIRST(&workers->workers);
        H2_WORKER_REMOVE(w);
    }
}

static int tq_in_list(h2_workers *workers, h2_task_queue *q)
{
    h2_task_queue *e;
    for (e = H2_TQ_LIST_FIRST(&workers->queues); 
         e != H2_TQ_LIST_SENTINEL(&workers->queues);
         e = H2_TQ_NEXT(e)) {
        if (e == q) {
            return 1;
        }
    }
    return 0;
}

apr_status_t h2_workers_schedule(h2_workers *workers, 
                                 h2_task_queue *q, h2_task *task)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, status, workers->s,
                     "h2_workers: scheduling task(%s)",
                     h2_task_get_id(task));
        if (h2_tq_empty(q)) {
            H2_TQ_LIST_INSERT_TAIL(&workers->queues, q);        
        }
        h2_tq_append(q, task);
        apr_thread_cond_signal(workers->task_added);
        
        if (workers->idle_worker_count <= 0 
            && workers->worker_count < workers->max_size) {
            ap_log_error(APLOG_MARK, APLOG_TRACE2, 0, workers->s,
                         "h2_workers: adding worker");
            add_worker(workers);
        }
        
        apr_thread_mutex_unlock(workers->lock);
    }
    return status;
}

apr_status_t h2_workers_unschedule(h2_workers *workers, 
                                   h2_task_queue *q, h2_task *task)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        status = APR_EAGAIN;
        if (task) {
            ap_log_error(APLOG_MARK, APLOG_TRACE1, 0, workers->s,
                         "h2_workers: unschedule task(%s)",
                         h2_task_get_id(task));
            status = h2_tq_remove(q, task);
        }
        else {
            if (tq_in_list(workers, q)) {
                H2_TQ_REMOVE(q);
                status = APR_SUCCESS;
            }
        }
        apr_thread_mutex_unlock(workers->lock);
    }
    
    return status;
}


void h2_workers_set_max_idle_secs(h2_workers *workers, int idle_secs)
{
    if (idle_secs <= 0) {
        ap_log_error(APLOG_MARK, APLOG_WARNING, 0, workers->s,
                     "h2_workers: max_worker_idle_sec value of %d"
                     " is not valid, ignored.", idle_secs);
        return;
    }
    apr_atomic_set32(&workers->max_idle_secs, idle_secs);
}

