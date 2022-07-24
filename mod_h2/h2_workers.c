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
#include "h2_worker.h"
#include "h2_workers.h"

static h2_task* pop_next_task(h2_workers *workers)
{
    // TODO: prio scheduling
    if (!H2_TASK_LIST_EMPTY(&workers->tasks)) {
        h2_task *task = H2_TASK_LIST_FIRST(&workers->tasks);
        H2_TASK_REMOVE(task);
        h2_task_set_started(task, 1);
        return task;
    }
    return NULL;
}

static apr_status_t get_task_next(h2_worker *worker, h2_task **ptask, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    *ptask = NULL;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        h2_task *task = NULL;
        status = APR_EOF;
        apr_time_t max_wait = apr_time_from_sec(apr_atomic_read32(&workers->max_idle_secs));
        apr_time_t start_wait = apr_time_now();
        
        ++workers->idle_worker_count;
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                     "h2_worker(%d): looking for work", h2_worker_get_id(worker));
        while (!h2_worker_is_aborted(worker) && !workers->aborted) {
            h2_task *task = pop_next_task(workers);
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
        h2_task_set_finished(task, 1);
        next_task = pop_next_task(workers);
        
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
        APR_RING_INIT(&workers->tasks, h2_task, link);
        
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
    while (!H2_TASK_LIST_EMPTY(&workers->tasks)) {
        h2_task *task = H2_TASK_LIST_FIRST(&workers->tasks);
        H2_TASK_REMOVE(task);
    }
    while (!H2_WORKER_LIST_EMPTY(&workers->workers)) {
        h2_worker *w = H2_WORKER_LIST_FIRST(&workers->workers);
        H2_WORKER_REMOVE(w);
    }
}

apr_status_t h2_workers_schedule(h2_workers *workers, h2_task *task)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, status, workers->s,
                     "h2_workers: scheduling task(%s)",
                     h2_task_get_id(task));
        
        H2_TASK_LIST_INSERT_TAIL(&workers->tasks, task);        
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

typedef struct{
    h2_task *task;
    h2_worker *found;
} find_task_ctx;

static int find_task(void *ctx, int id, void *entry, int index) 
{
    find_task_ctx *fctx = (find_task_ctx *)ctx;
    h2_worker *worker = (h2_worker *)entry;
    if (fctx->task == h2_worker_get_task(worker)) {
        fctx->found = worker;
        return 0;
    }
    return 1;
}


static apr_status_t join(h2_workers *workers, h2_task *task, int wait)
{
    if (h2_task_has_finished(task)) {
        return APR_SUCCESS;
    }
    
    if (!h2_task_has_started(task)) {
        /* might still be on scheduled tasks list */
        h2_task *t;
        for (t = H2_TASK_LIST_FIRST(&workers->tasks); 
             t != H2_TASK_LIST_SENTINEL(&workers->tasks);
             t = H2_TASK_NEXT(t)) {
            if (t == task) {
                H2_TASK_REMOVE(task);
                return APR_SUCCESS;
            }
        }
    }
    
    /* not on scheduled list, wait until not running */
    assert(h2_task_has_started(task));
    if (wait) {
        for (int i = 0; !h2_task_has_finished(task) && i < 100; ++i) {
            h2_task_interrupt(task);
            apr_thread_cond_t *iowait = task->io;
            if (iowait) {
                apr_thread_cond_timedwait(iowait, workers->lock, 20 * 1000);
            }
            else {
                if (!h2_task_has_finished(task)) {
                    ap_log_error(APLOG_MARK, APLOG_WARNING, 0, workers->s,
                                 "h2_workers: join task(%s) started, but "
                                 "not finished, no worker found",
                                 h2_task_get_id(task));
                }
                break;
            }
        }
    }
    return h2_task_has_finished(task)? APR_SUCCESS : APR_EAGAIN;
}

apr_status_t h2_workers_join(h2_workers *workers, h2_task *task, int wait)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_TRACE1, 0, workers->s,
                     "h2_workers: join task(%s) started",
                     h2_task_get_id(task));
        status = join(workers, task, wait);
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

