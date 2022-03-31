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
#include <http_connection.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_bucket_queue.h"

static void bucket_free(void *entry)
{
    h2_bucket_destroy((h2_bucket *)entry);
}

void h2_bucket_queue_init(h2_bucket_queue *q)
{
    APR_RING_INIT(&q->ring, h2_bucket, link);
    q->aborted = 0;
}

void h2_bucket_queue_cleanup(h2_bucket_queue *q)
{
    h2_bucket *b;
    
    while (!H2_QUEUE_EMPTY(q)) {
        b = H2_QUEUE_FIRST(q);
        H2_BUCKET_REMOVE(b);						\
        h2_bucket_destroy(b);
    }
}

h2_bucket_queue *h2_bucket_queue_create(apr_pool_t *pool)
{
    h2_bucket_queue *q = apr_pcalloc(pool, sizeof(h2_bucket_queue));
    if (q) {
        h2_bucket_queue_init(q);
    }
    return q;
}

typedef struct {
    int stream_id;
    apr_size_t size;
} count_ctx;

int count_stream(void *puser, int id, void *entry, int index)
{
    count_ctx *ctx = (count_ctx*)puser;
    if (ctx->stream_id == id) {
        h2_bucket *bucket = (h2_bucket*)entry;
        ctx->size += bucket->data_len;
    }
    return 1;
}

apr_size_t h2_bucket_queue_get_length(h2_bucket_queue *q) {
    apr_status_t status = APR_SUCCESS;
    apr_size_t total = 0;
    h2_bucket *b;
    
    for (b = H2_QUEUE_FIRST(q);
         b != H2_QUEUE_SENTINEL(q);
         b = H2_BUCKET_NEXT(b))
    {
        total += b->data_len;
    }
        
    return total;
}

void h2_bucket_queue_destroy(h2_bucket_queue *q)
{
    h2_bucket_queue_cleanup(q);
}

void h2_bucket_queue_abort(h2_bucket_queue *q)
{
    q->aborted = 1;
}

apr_status_t h2_bucket_queue_prepend(h2_bucket_queue *q, h2_bucket *b)
{
    if (q->aborted) {
        return APR_ECONNABORTED;
    }
    H2_QUEUE_INSERT_HEAD(q, b);
    return APR_SUCCESS;
}

apr_status_t h2_bucket_queue_pop(h2_bucket_queue *q, h2_bucket **pb)
{
    *pb = NULL;
    if (!H2_QUEUE_EMPTY(q)) {
        h2_bucket *b = H2_QUEUE_FIRST(q);
        H2_BUCKET_REMOVE(b);
        if (h2_bucket_is_eos(b)) {
            h2_bucket_destroy(b);
            return APR_EOF;
        }
        *pb = b;
        return APR_SUCCESS;
    }
    return APR_EAGAIN;
}

apr_status_t h2_bucket_queue_append(h2_bucket_queue *q, h2_bucket *b)
{
    if (q->aborted) {
        return APR_ECONNABORTED;
    }
    H2_QUEUE_INSERT_TAIL(q, b);
    return APR_SUCCESS;
}

apr_status_t h2_bucket_queue_pass(h2_bucket_queue *q,
                                  h2_bucket_queue *other)
{
    while (!H2_QUEUE_EMPTY(other)) {
        h2_bucket *b = H2_QUEUE_FIRST(other);
        H2_BUCKET_REMOVE(b);
        H2_QUEUE_INSERT_TAIL(q, b);
    }
    return APR_SUCCESS;
}


apr_status_t h2_bucket_queue_append_eos(h2_bucket_queue *q)
{
    h2_bucket *eos = h2_bucket_alloc_eos();
    if (!eos) {
        return APR_ENOMEM;
    }
    H2_QUEUE_INSERT_TAIL(q, eos);
    return APR_SUCCESS;
}

int h2_bucket_queue_has_eos(h2_bucket_queue *q)
{
    h2_bucket *b;
    for (b = H2_QUEUE_FIRST(q);
         b != H2_QUEUE_SENTINEL(q);
         b = H2_BUCKET_NEXT(b))
    {
        if (h2_bucket_is_eos(b)) {
            return 1;
        }
    }
    return 0;
}

int h2_bucket_queue_is_eos(h2_bucket_queue *q)
{
    return (!H2_QUEUE_EMPTY(q) && h2_bucket_is_eos(H2_QUEUE_FIRST(q)));
}

int h2_bucket_queue_is_empty(h2_bucket_queue *q)
{
    return H2_QUEUE_EMPTY(q);
}

apr_status_t h2_bucket_queue_consume(h2_bucket_queue *q, 
                                     apr_bucket_brigade *bb, 
                                     apr_size_t buf_max)
{
    apr_status_t status = APR_SUCCESS;
    
    while (!APR_BRIGADE_EMPTY(bb) 
           && (status == APR_SUCCESS)) {
        apr_bucket* bucket = APR_BRIGADE_FIRST(bb);
        
        if (APR_BUCKET_IS_METADATA(bucket)) {
            if (APR_BUCKET_IS_EOS(bucket)) {
                h2_bucket_queue_append_eos(q);
            }
            else {
                /* ignore */
            }
        }
        else {
            const char* data = NULL;
            apr_size_t data_len = 0;

            if (h2_bucket_queue_get_length(q) >= buf_max) {
                return APR_INCOMPLETE;
            }
            
            status = apr_bucket_read(bucket, &data, &data_len,
                                     APR_NONBLOCK_READ);
            if (APR_STATUS_IS_EAGAIN(status)) {
                status = apr_bucket_read(bucket, &data, &data_len,
                                         APR_BLOCK_READ);
            }
            
            if (status == APR_SUCCESS) {
                if (data_len > 0) {
                    h2_bucket *b = h2_bucket_alloc(data_len);
                    h2_bucket_append(b, data, data_len);
                    status = h2_bucket_queue_append(q, b);
                }
            }
        }
        
        apr_bucket_delete(bucket);
    }
    
    return status;
}

