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
#include "h2_queue.h"

typedef enum {
    H2_BQ_EV_BEFORE_APPEND,
    H2_BQ_EV_BEFORE_PUSH
} h2_bucket_queue_event_t;

struct h2_bucket_queue;

typedef void h2_bucket_queue_event_cb(struct h2_bucket_queue *queue,
                                      h2_bucket_queue_event_t etype,
                                      h2_bucket *bucket,
                                      int stream_id,
                                      void *ev_ctx);

typedef struct h2_bucket_queue {
    h2_queue *queue;
    apr_thread_mutex_t *lock;
    apr_thread_cond_t *has_data;
    
    h2_bucket_queue_event_cb *ev_cb;
    void *ev_ctx;
} h2_bucket_queue;


h2_bucket_queue *h2_bucket_queue_create(apr_pool_t *pool);

void h2_bucket_queue_destroy(h2_bucket_queue *q);

apr_status_t h2_bucket_queue_append(h2_bucket_queue *q, h2_bucket *bucket,
                                    int stream_id);

apr_status_t h2_bucket_queue_push(h2_bucket_queue *q, h2_bucket *bucket,
                                  int stream_id);

apr_status_t h2_bucket_queue_append_eos(h2_bucket_queue *q, int stream_id);

int h2_bucket_queue_is_empty(h2_bucket_queue *q);
int h2_bucket_queue_has_eos_for(h2_bucket_queue *q, int stream_id);
int h2_bucket_queue_has_buckets_for(h2_bucket_queue *q, int stream_id);

apr_status_t h2_bucket_queue_pop(h2_bucket_queue *q,
                                 apr_read_type_e block,
                                 int stream_id,
                                 h2_bucket **pbucket);

void h2_bucket_queue_set_event_cb(h2_bucket_queue *queue,
                                  h2_bucket_queue_event_cb *callback,
                                  void *ev_ctx);

#endif /* defined(__mod_h2__h2_bucket_queue__) */
