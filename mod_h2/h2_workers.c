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
#include "h2_mplx.h"
#include "h2_task.h"
#include "h2_task_queue.h"
#include "h2_worker.h"
#include "h2_workers.h"

static int in_list(h2_workers *workers, h2_mplx *m)
{
    h2_mplx *e;
    for (e = H2_MPLX_LIST_FIRST(&workers->mplxs); 
         e != H2_MPLX_LIST_SENTINEL(&workers->mplxs);
         e = H2_MPLX_NEXT(e)) {
        if (e == m) {
            return 1;
        }
    }
    return 0;
}


static h2_mplx* pop_next_mplx(h2_workers *workers, h2_worker *worker)
{
    (void)worker;
    if (!H2_MPLX_LIST_EMPTY(&workers->mplxs)) {
        h2_mplx *m = H2_MPLX_LIST_FIRST(&workers->mplxs);
        H2_MPLX_REMOVE(m);
        return m;
    }
    return NULL;
}


static apr_status_t get_mplx_next(h2_worker *worker, h2_mplx **pmplx, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    *pmplx = NULL;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        status = APR_EOF;
        apr_time_t max_wait = apr_time_from_sec(apr_atomic_read32(&workers->max_idle_secs));
        apr_time_t start_wait = apr_time_now();
        
        ++workers->idle_worker_count;
        ap_log_error(APLOG_MARK, APLOG_TRACE1, 0, workers->s,
                     "h2_worker(%d): looking for work", h2_worker_get_id(worker));
        while (!h2_worker_is_aborted(worker) && !workers->aborted) {
            h2_mplx *m = pop_next_mplx(workers, worker);
            if (m) {
                *pmplx = m;
                status = APR_SUCCESS;
                ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                             "h2_worker(%d): start mplx(%ld)",
                             h2_worker_get_id(worker), m->id);
                break;
            }
            
            /* Need to wait for either a new mplx to arrive or, if we
             * are not at the minimum workers count, wait our max idle
             * time until we reduce the number of workers */
            if (workers->worker_count > workers->min_size) {
                apr_time_t now = apr_time_now();
                if (now >= (start_wait + max_wait)) {
                    /* waited long enough without getting a task. */
                    status = APR_TIMEUP;
                }
                else {
                    ap_log_error(APLOG_MARK, APLOG_TRACE1, 0, workers->s,
                                 "h2_worker(%d): waiting signal, worker_count=%d",
                                 h2_worker_get_id(worker), (int)workers->worker_count);
                    status = apr_thread_cond_timedwait(workers->mplx_added,
                                                       workers->lock, max_wait);
                }
                if (status == APR_TIMEUP) {
                    /* waited long enough */
                    if (workers->worker_count > workers->min_size) {
                        ap_log_error(APLOG_MARK, APLOG_TRACE1, 0, workers->s,
                                     "h2_workers: aborting idle worker");
                        h2_worker_abort(worker);
                        break;
                    }
                }
            }
            else {
                ap_log_error(APLOG_MARK, APLOG_TRACE1, 0, workers->s,
                             "h2_worker(%d): waiting signal (eternal), worker_count=%d",
                             h2_worker_get_id(worker), (int)workers->worker_count);
                apr_thread_cond_wait(workers->mplx_added, workers->lock);
            }
        }
        --workers->idle_worker_count;
        apr_thread_mutex_unlock(workers->lock);
    }
    return status;
}

static h2_mplx *mplx_done(h2_worker *worker, h2_mplx *m,
                          apr_status_t mplx_status, void *ctx)
{
    h2_workers *workers = (h2_workers *)ctx;
    h2_mplx *next_mplx = NULL;
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        /* If EAGAIN and not empty, place into list again */
        if (mplx_status == APR_EAGAIN && !in_list(workers, m)) {
            H2_MPLX_LIST_INSERT_TAIL(&workers->mplxs, m);
            apr_thread_cond_signal(workers->mplx_added);
        }
        else {
            h2_mplx_release(m);
        }
        next_mplx = pop_next_mplx(workers, worker);
        
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, workers->s,
                     "h2_worker(%d): mplx(%ld) done, next(%ld)",
                     h2_worker_get_id(worker), m->id,
                     next_mplx? next_mplx->id : -1);
        
        apr_thread_mutex_unlock(workers->lock);
    }
    return next_mplx;
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
                                    get_mplx_next, mplx_done, worker_done,
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
    AP_DEBUG_ASSERT(s);
    AP_DEBUG_ASSERT(pool);
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
        APR_RING_INIT(&workers->mplxs, h2_mplx, link);
        
        status = apr_thread_mutex_create(&workers->lock,
                                         APR_THREAD_MUTEX_DEFAULT,
                                         workers->pool);
        if (status == APR_SUCCESS) {
            status = apr_thread_cond_create(&workers->mplx_added, workers->pool);
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
    if (workers->mplx_added) {
        apr_thread_cond_destroy(workers->mplx_added);
        workers->mplx_added = NULL;
    }
    if (workers->lock) {
        apr_thread_mutex_destroy(workers->lock);
        workers->lock = NULL;
    }
    while (!H2_MPLX_LIST_EMPTY(&workers->mplxs)) {
        h2_mplx *m = H2_MPLX_LIST_FIRST(&workers->mplxs);
        H2_MPLX_REMOVE(m);
    }
    while (!H2_WORKER_LIST_EMPTY(&workers->workers)) {
        h2_worker *w = H2_WORKER_LIST_FIRST(&workers->workers);
        H2_WORKER_REMOVE(w);
    }
}

apr_status_t h2_workers_register(h2_workers *workers, struct h2_mplx *m)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, status, workers->s,
                     "h2_workers: register mplx(%ld)", m->id);
        if (!in_list(workers, m)) {
            H2_MPLX_LIST_INSERT_TAIL(&workers->mplxs, m);
            h2_mplx_reference(m);
        }
        apr_thread_cond_signal(workers->mplx_added);
        
        if (workers->idle_worker_count <= 0 
            && workers->worker_count < workers->max_size) {
            ap_log_error(APLOG_MARK, APLOG_TRACE1, 0, workers->s,
                         "h2_workers: got %d worker, adding 1", 
                         workers->worker_count);
            add_worker(workers);
        }
        
        apr_thread_mutex_unlock(workers->lock);
    }
    return status;
}

apr_status_t h2_workers_unregister(h2_workers *workers, struct h2_mplx *m)
{
    apr_status_t status = apr_thread_mutex_lock(workers->lock);
    if (status == APR_SUCCESS) {
        status = APR_EAGAIN;
        if (in_list(workers, m)) {
            H2_MPLX_REMOVE(m);
            h2_mplx_release(m);
            status = APR_SUCCESS;
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

