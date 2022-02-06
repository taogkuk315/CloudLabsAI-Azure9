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
    
    int worker_count;
    struct h2_queue *workers;
    struct h2_queue *tasks_scheduled;
    
    volatile apr_uint32_t max_idle_secs;
    volatile apr_uint32_t idle_worker_count;
    
    struct apr_thread_mutex_t *lock;
    struct apr_thread_cond_t *task_added;
};

static h2_task* pop_next_task(h2_workers *workers)
{
    // TODO: prio scheduling
    h2_task *task = h2_queue_pop(workers->tasks_scheduled);
    if (task) {
        h2_task_set_started(task, 1);
    }
    return task;
}

static apr_status_t get_task_next(h2_worker *worker, h2_task **ptask, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        h2_task *task = NULL;
        status = APR_EOF;
        apr_time_t max_wait = apr_time_from_sec(apr_atomic_read32(&workers->max_idle_secs));
        apr_time_t start_wait = apr_time_now();
        
        ++workers->idle_worker_count;
        while (!h2_worker_is_aborted(worker) && !workers->aborted) {
            h2_task *task = pop_next_task(workers);
            if (task) {
                *ptask = task;
                status = APR_SUCCESS;
                ap_log_error(APLOG_MARK, APLOG_DEBUG, status, workers->s,
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
                    status = apr_thread_cond_timedwait(workers->task_added,
                                                       workers->lock, max_wait);
                }
                if (status == APR_TIMEUP) {
                    /* waited long enough */
                    if (workers->worker_count > workers->min_size) {
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

static h2_task *task_done(h2_worker *worker, h2_task *task,
                          apr_status_t task_status, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    h2_task *next_task = NULL;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, status, workers->s,
                     "h2_worker(%d): task(%s) done",
                     h2_worker_get_id(worker), h2_task_get_id(task));
        
        h2_task_set_finished(task, 1);
        next_task = pop_next_task(workers);
        
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
        h2_queue_remove(workers->workers, worker);
        workers->worker_count = h2_queue_size(workers->workers);
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
    return h2_queue_append(workers->workers, w);
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
        
        workers->workers = h2_queue_create(workers->pool, NULL);
        workers->tasks_scheduled = h2_queue_create(workers->pool, NULL);
        
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
                     "h2_workers: scheduling task(%s)",
                     h2_task_get_id(task));
        
        h2_queue_append(workers->tasks_scheduled, task);
        
        h2_worker *worker = NULL;
        if (workers->idle_worker_count > 0) {
            apr_thread_cond_signal(workers->task_added);
        }
        
        if (worker == NULL
            && workers->worker_count < workers->max_size) {
            ap_log_error(APLOG_MARK, APLOG_TRACE2, status, workers->s,
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

h2_worker *h2_workers_get_task_worker(h2_workers *workers, h2_task *task)
{
    find_task_ctx ctx = { task, NULL };
    h2_queue_iter(workers->workers, find_task, &ctx);
    return ctx.found;
}

apr_status_t h2_workers_join(h2_workers *workers, h2_task *task, int wait)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_TRACE1, 0, workers->s,
                     "h2_workers: join task(%s) started",
                     h2_task_get_id(task));
        
        if (!h2_queue_remove(workers->tasks_scheduled, task)) {
            /* not on scheduled list, wait until not running */
            assert(h2_task_has_started(task));
            for (int i = 0; wait && !h2_task_has_finished(task) && i < 100; ++i) {
                h2_worker *worker = h2_workers_get_task_worker(workers, task);
                h2_task_interrupt(task);
                if (worker) {
                    apr_thread_cond_timedwait(h2_worker_get_cond(worker), 
                                              workers->lock, 20 * 1000);
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
            if (!h2_task_has_finished(task)) {
                status = APR_EAGAIN;
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
