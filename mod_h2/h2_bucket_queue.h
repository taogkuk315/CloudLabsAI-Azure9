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

#ifndef __mod_h2__h2_bucket_queue__
#define __mod_h2__h2_bucket_queue__

#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>

#include "h2_bucket.h"

typedef struct h2_bucket_queue {
    apr_pool_t *pool;
    struct h2_qdata *first;
    struct h2_qdata *last;
    struct h2_qdata *free;
    
    apr_thread_mutex_t *lock;
    apr_thread_cond_t *has_data;
    int terminated;
} h2_bucket_queue;

apr_status_t h2_bucket_queue_init(h2_bucket_queue *q, apr_pool_t *pool);

void h2_bucket_queue_term(h2_bucket_queue *q);

apr_status_t h2_bucket_queue_push(h2_bucket_queue *q, h2_bucket *bucket,
                                  int stream_id);

apr_status_t h2_bucket_queue_push_eos(h2_bucket_queue *q,
                                  int stream_id);

int h2_bucket_queue_has_eos_for(h2_bucket_queue *q, int stream_id);

apr_status_t h2_bucket_queue_pop(h2_bucket_queue *q, apr_read_type_e block,
                                 h2_bucket **pbucket, int *stream_id);

apr_status_t h2_bucket_queue_stream_pop(h2_bucket_queue *q, apr_read_type_e block,
                                      int stream_id, h2_bucket **pbucket);

#endif /* defined(__mod_h2__h2_bucket_queue__) */
