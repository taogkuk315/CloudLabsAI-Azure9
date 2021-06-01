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

/**
 * A queue, or ordered list, of h2_bucket structures with the additional
 * twist that it associates buckets with integer ids. It is possible to
 * retrieve buckets by their associated id.
 *
 * The queue is thread safe and allows blocking/non-blocking reads.
 * 
 * The buckets passed into the queue are not copied. The queue takes
 * ownership and will destroy all buckets when it is destroyed itself.
 * Buckets retrieved will be owned by the called.
 *
 * It is possible to push an "end-of-stream" for a integer id, signalling
 * that there will be no more buckets coming afterwards.
 *
 * The queue offers an event callback when buckets are placed into the
 * queue.
 */

struct h2_bucket;
struct h2_queue;
struct apr_thread_mutex_t;
struct apr_thread_cond_t;

/* Event types for this queue, triggered just before a bucket
 * is added to the queue to the tail or head of the queue. */
typedef enum {
    H2_BQ_EV_BEFORE_APPEND,
    H2_BQ_EV_BEFORE_PUSH
} h2_bucket_queue_event_t;

struct h2_bucket_queue;

typedef void h2_bucket_queue_event_cb(struct h2_bucket_queue *queue,
                                      h2_bucket_queue_event_t etype,
                                      struct h2_bucket *bucket,
                                      int stream_id, int is_only_one,
                                      void *ev_ctx);

typedef struct h2_bucket_queue {
    struct h2_queue *queue;
    struct apr_thread_mutex_t *lock;
    struct apr_thread_cond_t *has_data;
    
    h2_bucket_queue_event_cb *ev_cb;
    void *ev_ctx;
} h2_bucket_queue;

/* Create a new queue using the given memory pool. The queue will
 * reuse allocated memory, so memory footprint varies with queue length,
 * not number of buckets placed. */
h2_bucket_queue *h2_bucket_queue_create(apr_pool_t *pool);

/* Destroys this queue and all buckets it still contains. */
void h2_bucket_queue_destroy(h2_bucket_queue *q);

/* Append a bucket, associated with the given id, at the end of the queue. */
apr_status_t h2_bucket_queue_append(h2_bucket_queue *q,
                                    struct h2_bucket *bucket,
                                    int stream_id);

/* Place the bucket for the given id at the head of the queue. */
apr_status_t h2_bucket_queue_push(h2_bucket_queue *q,
                                  struct h2_bucket *bucket,
                                  int stream_id);

/* Append an "End-of-Stream" marker to the queue. There are no more buckets
 * expected to be appended afterwards, although the queue does not check for
 * this. */
apr_status_t h2_bucket_queue_append_eos(h2_bucket_queue *q, int stream_id);

/* Return != 0 iff there are no buckets in the queue. */
int h2_bucket_queue_is_empty(h2_bucket_queue *q);
/* Return != 0 iff there is an eos for the given id in the queue. The queue
 * may contain buckets for the id before that still. */
int h2_bucket_queue_has_eos_for(h2_bucket_queue *q, int stream_id);
/* Return != 0 iff there are buckets for the given id in the queue. */
int h2_bucket_queue_has_buckets_for(h2_bucket_queue *q, int stream_id);

/* Get the first bucket from the head of the queue for the given id. If block
 * is APR_BLOCK_READ, this will not return until a bucket arrives (or eos).
 * With APR_NONBLOCK_READ, it will return APR_EAGAIN. */
apr_status_t h2_bucket_queue_pop(h2_bucket_queue *q,
                                 apr_read_type_e block,
                                 int stream_id,
                                 struct h2_bucket **pbucket);

void h2_bucket_queue_set_event_cb(h2_bucket_queue *queue,
                                  h2_bucket_queue_event_cb *callback,
                                  void *ev_ctx);

#endif /* defined(__mod_h2__h2_bucket_queue__) */
