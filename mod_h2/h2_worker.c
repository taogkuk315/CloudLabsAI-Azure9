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

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_task.h"
#include "h2_worker.h"

static void *execute(apr_thread_t *thread, void *wctx)
{
    h2_worker *worker = (h2_worker *)wctx;
    apr_status_t status = APR_SUCCESS;
    
    while (!worker->aborted) {
        status = worker->get_next(worker, &worker->current,worker->ctx);
        if (status == APR_SUCCESS) {
            apr_status_t status = h2_task_do(worker->current);
            worker->task_done(worker, worker->current, status, worker->ctx);
            worker->current = NULL;
        }
    }
    
    worker->worker_done(worker, worker->ctx);
    apr_thread_exit(thread, status);
    return NULL;
}

h2_worker *h2_worker_create(int id,
                            apr_pool_t *pool,
                            apr_threadattr_t *attr,
                            h2_worker_task_next_fn *get_next,
                            h2_worker_task_done_fn *task_done,
                            h2_worker_done_fn *worker_done,
                            void *ctx)
{
    h2_worker *w = apr_pcalloc(pool, sizeof(h2_worker));
    if (w) {
        w->id = id;
        w->pool = pool;
        w->get_next = get_next;
        w->task_done = task_done;
        w->worker_done = worker_done;
        w->ctx = ctx;
        
        apr_thread_create(&w->thread, attr, execute, w, pool);
    }
    return w;
}

apr_status_t h2_worker_destroy(h2_worker *worker)
{
    return APR_SUCCESS;
}

int h2_worker_is_aborted(h2_worker *worker)
{
    return worker->aborted;
}

