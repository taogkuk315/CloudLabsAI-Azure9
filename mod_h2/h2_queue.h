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

#ifndef __mod_h2__h2_queue__
#define __mod_h2__h2_queue__

typedef void *(*h2_queue_match_fn)(void *ctx, int id, void *entry);
typedef void (*h2_queue_free_fn)(void *entry);

/**
 * A simple double linked list, operating on a APR memory pool, that
 * has a memory footprint controlled by the queue length, not the 
 * number of pop/push operations.
 *
 * Each entry can be associated with an integer id. The id can be used
 * for searching and manipulations.
 * 
 * This queue is *not* thread safe.
 */
typedef struct h2_queue {
    apr_pool_t *pool;
    struct h2_qdata *first;
    struct h2_qdata *last;
    struct h2_qdata *free;
    
    int terminated;
    h2_queue_free_fn free_fn;
} h2_queue;


#define H2_QUEUE_ID_NONE        (-1)

h2_queue *h2_queue_create(apr_pool_t *pool, h2_queue_free_fn free_fn);
void h2_queue_destroy(h2_queue *q);
void h2_queue_term(h2_queue *q);

apr_status_t h2_queue_append(h2_queue *q, void *entry);
apr_status_t h2_queue_append_id(h2_queue *q, int id, void *entry);

apr_status_t h2_queue_push(h2_queue *q, void *entry);
apr_status_t h2_queue_push_id(h2_queue *q, int id, void *entry);

void *h2_queue_find(h2_queue *q, h2_queue_match_fn find, void *ctx);
void *h2_queue_find_id(h2_queue *q, int id);

void *h2_queue_pop(h2_queue *q);
void *h2_queue_pop_id(h2_queue *q, int id);
void *h2_queue_pop_find(h2_queue *q, h2_queue_match_fn find, void *ctx);

void *h2_queue_remove(h2_queue *q, void *entry);
void h2_queue_remove_all(h2_queue *q);

int h2_queue_is_terminated(h2_queue *q);
int h2_queue_is_empty(h2_queue *q);

#endif /* defined(__mod_h2__h2_queue__) */
