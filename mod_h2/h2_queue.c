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

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_queue.h"

typedef struct h2_qdata {
    struct h2_qdata *next;
    struct h2_qdata *prev;
    void *entry;
    int id;
} h2_qdata;

h2_queue *h2_queue_create(apr_pool_t *pool, h2_queue_free_fn free_fn)
{
    h2_queue *q = apr_pcalloc(pool, sizeof(h2_queue));
    if (q) {
        q->pool = pool;
        q->first = q->last = q->free = NULL;
        q->free_fn = free_fn;
    }
    return q;
}

void h2_queue_destroy(h2_queue *q)
{
    assert(q);
    q->last = NULL;
    while (q->first) {
        h2_qdata *qdata = q->first;
        q->first = qdata->next;
        if (qdata->entry) {
            if (q->free_fn) {
                q->free_fn(qdata->entry);
            }
            qdata->entry = NULL;
        }
    }
}

void h2_queue_abort(h2_queue *q)
{
    assert(q);
     q->aborted = 1;
    h2_queue_remove_all(q);
}

static void queue_unlink(h2_queue *q, h2_qdata *qdata)
{
    assert(q);
    if (q->first == qdata) {
        /* at the head */
        q->first = qdata->next;
        if (q->first) {
            q->first->prev = NULL;
        }
        else {
            /* was the last */
            q->last = NULL;
        }
    }
    else if (q->last == qdata) {
        /* at the tail */
        q->last = qdata->prev;
        if (q->last) {
            q->last->next = NULL;
        }
        else {
            /* if qdata was the last, we should not be here */
            assert(0);
        }
    }
    else {
        /* in the middle */
        qdata->next->prev = qdata->prev;
        qdata->prev->next = qdata->next;
    }
    qdata->next = qdata->prev = NULL;
}

static void queue_free(h2_queue *q, h2_qdata *qdata) {
    assert(q);
    queue_unlink(q, qdata);
    memset(qdata, 0, sizeof(h2_qdata));
    qdata->next = q->free;
    q->free = qdata;
}

static void *match_any(void *ctx, int id, void *entry)
{
    return entry;
}

static void *match_id(void *ctx, int id, void *entry)
{
    int match_id = *((int*)ctx);
    return (match_id == id)? entry : NULL;
}

static void *match_entry(void *match_entry, int id, void *entry)
{
    return (match_entry == entry)? entry : NULL;
}

static h2_qdata *h2_queue_find_int(h2_queue *q,
                                   h2_queue_match_fn match, void *ctx)
{
    assert(q);
    for (h2_qdata *qdata = q->first; qdata; qdata = qdata->next) {
        void *entry = match(ctx, qdata->id, qdata->entry);
        if (entry) {
            return qdata;
        }
    }
    return NULL;
}

void *h2_queue_find(h2_queue *q, h2_queue_match_fn match, void *ctx)
{
    assert(q);
    h2_qdata *qdata = h2_queue_find_int(q, match, ctx);
    return qdata? qdata->entry : NULL;
}

void *h2_queue_find_id(h2_queue *q, int id)
{
    assert(q);
    for (h2_qdata *qdata = q->first; qdata; qdata = qdata->next) {
        if (qdata->id == id) {
            return qdata->entry;
        }
    }
    return NULL;
}

void h2_queue_iter(h2_queue *q, h2_queue_iter_fn iter, void *ctx)
{
    assert(q);
    int index = 0;
    h2_qdata *next = q->first;
    while (next) {
        /* This needs to work should the iterator remove the current entry */
        h2_qdata *qdata = next;
        next = qdata->next;
        if (!iter(ctx, qdata->id, qdata->entry, index)) {
            break;
        }
    }
}

void *h2_queue_pop_find(h2_queue *q, h2_queue_match_fn find, void *ctx)
{
    assert(q);
    void *entry = NULL;
    h2_qdata *qdata = h2_queue_find_int(q, find, ctx);
    if (qdata) {
        entry = qdata->entry;
        queue_free(q, qdata);
    }
    return entry;
}

void *h2_queue_pop(h2_queue *q)
{
    return h2_queue_pop_find(q, match_any, NULL);
}

void *h2_queue_pop_id(h2_queue *q, int id)
{
    return h2_queue_pop_find(q, match_id, &id);
}

apr_status_t h2_queue_append(h2_queue *q, void *entry)
{
    return h2_queue_append_id(q, H2_QUEUE_ID_NONE, entry);
}

apr_status_t h2_queue_append_id(h2_queue *q, int id, void *entry)
{
    assert(q);
    if (q->aborted) {
        return APR_EOF;
    }
    else {
        h2_qdata *qdata = q->free;
        if (qdata) {
            q->free = qdata->next;
            memset(qdata, 0, sizeof(h2_qdata));
        }
        else {
            qdata = apr_pcalloc(q->pool, sizeof(h2_qdata));
        }
        
        qdata->entry = entry;
        qdata->id = id;
        
        if (q->last) {
            q->last->next = qdata;
            qdata->prev = q->last;
            q->last = qdata;
        }
        else {
            assert(!q->first);
            q->first = q->last = qdata;
        }
    }
    return APR_SUCCESS;
}

apr_status_t h2_queue_push(h2_queue *q, void *entry)
{
    return h2_queue_push_id(q, H2_QUEUE_ID_NONE, entry);
}

apr_status_t h2_queue_push_id(h2_queue *q, int id, void *entry)
{
    assert(q);
    if (q->aborted) {
        return APR_EOF;
    }
    else {
        h2_qdata *qdata = q->free;
        if (qdata) {
            q->free = qdata->next;
            memset(qdata, 0, sizeof(h2_qdata));
        }
        else {
            qdata = apr_pcalloc(q->pool, sizeof(h2_qdata));
        }
        
        qdata->entry = entry;
        qdata->id = id;
        
        if (q->first) {
            q->first->prev = qdata;
            qdata->next = q->first;
            q->first = qdata;
        }
        else {
            assert(!q->last);
            q->first = q->last = qdata;
        }
    }
    return APR_SUCCESS;
}

void *h2_queue_remove(h2_queue *q, void *entry)
{
    assert(q);
    return h2_queue_pop_find(q, match_entry, entry);
}

void h2_queue_remove_all(h2_queue *q)
{
    assert(q);
    while(q->first) {
        queue_free(q, q->first);
    }
}

int h2_queue_is_aborted(h2_queue *q)
{
    assert(q);
    return q->aborted;
}

int h2_queue_is_empty(h2_queue *q)
{
    assert(q);
    return q->first == NULL;
}

apr_size_t h2_queue_size(h2_queue *q)
{
    assert(q);
    apr_size_t size = 0;
    for (h2_qdata *e = q->first; e; e = e->next) {
        ++size;
    }
    return size;
}



