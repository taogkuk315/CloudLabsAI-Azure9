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

struct h2_bucket_queue;

typedef struct h2_bucket_queue {
    APR_RING_HEAD(h2_bucket_ring, h2_bucket) ring;
    int aborted;
} h2_bucket_queue;

/**
 * The magic pointer value that indicates the head of the queue
 * @param  q The queue
 * @return The magic pointer value
 */
#define H2_QUEUE_SENTINEL(q)	APR_RING_SENTINEL(&(q)->ring, h2_bucket, link)

/**
 * Determine if the bucket queue is empty
 * @param q The queue to check
 * @return true or false
 */
#define H2_QUEUE_EMPTY(q)	APR_RING_EMPTY(&(q)->ring, h2_bucket, link)

/**
 * Return the first bucket in a queue
 * @param q The queue to query
 * @return The first bucket in the queue
 */
#define H2_QUEUE_FIRST(q)	APR_RING_FIRST(&(q)->ring)

/**
 * Return the last bucket in a queue
 * @param q The queue to query
 * @return The last bucket in the queue
 */
#define H2_QUEUE_LAST(q)	APR_RING_LAST(&(q)->ring)

/*
 * define H2_BUCKET_DEBUG if you want your queues to be checked for
 * validity at every possible instant.  this will slow your code down
 * substantially but is a very useful debugging tool.
 */
#ifdef H2_BUCKET_DEBUG

#define H2_QUEUE_CHECK_CONSISTENCY(b)				\
APR_RING_CHECK_CONSISTENCY(&(b)->ring, h2_bucket, link)

#define H2_BUCKET_CHECK_CONSISTENCY(e)					\
APR_RING_CHECK_ELEM_CONSISTENCY((e), h2_bucket, link)

#else
/**
 * checks the ring pointers in a bucket queue for consistency.  an
 * abort() will be triggered if any inconsistencies are found.
 *   note: this is a no-op unless H2_BUCKET_DEBUG is defined.
 * @param b The queue
 */
#define H2_QUEUE_CHECK_CONSISTENCY(b)
/**
 * checks the queue a bucket is in for ring consistency.  an
 * abort() will be triggered if any inconsistencies are found.
 *   note: this is a no-op unless H2_BUCKET_DEBUG is defined.
 * @param e The bucket
 */
#define H2_BUCKET_CHECK_CONSISTENCY(e)
#endif

/**
 * Insert a single bucket at the front of a queue
 * @param q The queue to add to
 * @param e The bucket to insert
 */
#define H2_QUEUE_INSERT_HEAD(q, e) do {				\
	h2_bucket *ap__b = (e);                                        \
	APR_RING_INSERT_HEAD(&(q)->ring, ap__b, h2_bucket, link);	\
        H2_QUEUE_CHECK_CONSISTENCY((q));				\
    } while (0)

/**
 * Insert a single bucket at the end of a queue
 * @param q The queue to add to
 * @param e The bucket to insert
 */
#define H2_QUEUE_INSERT_TAIL(q, e) do {				\
	h2_bucket *ap__b = (e);					\
	APR_RING_INSERT_TAIL(&(q)->ring, ap__b, h2_bucket, link);	\
        H2_QUEUE_CHECK_CONSISTENCY((q));				\
    } while (0)


/* Create a new queue using the given memory pool. The queue will
 * reuse allocated memory, so memory footprint varies with queue length,
 * not number of buckets placed. 
 */
h2_bucket_queue *h2_bucket_queue_create(apr_pool_t *pool);
void h2_bucket_queue_init(h2_bucket_queue *q);

/* Destroys this queue and all buckets it still contains. */
void h2_bucket_queue_destroy(h2_bucket_queue *q);
void h2_bucket_queue_cleanup(h2_bucket_queue *q);

void h2_bucket_queue_abort(h2_bucket_queue *q);

/* Get the number of bytes in payload currently in the queue.
 */
apr_size_t h2_bucket_queue_get_length(h2_bucket_queue *q);

/* Append a bucket at the end of the queue. */
apr_status_t h2_bucket_queue_append(h2_bucket_queue *q,
                                    struct h2_bucket *bucket);
apr_status_t h2_bucket_queue_pass(h2_bucket_queue *q,
                                  h2_bucket_queue *other);

/* Place the bucket at the head of the queue. */
apr_status_t h2_bucket_queue_prepend(h2_bucket_queue *q,
                                  struct h2_bucket *bucket);

/* Append an "End-of-Stream" marker to the queue. 
 */
apr_status_t h2_bucket_queue_append_eos(h2_bucket_queue *q);

/* Return != 0 iff there are no buckets in the queue. */
int h2_bucket_queue_is_empty(h2_bucket_queue *q);

/* Return != 0 iff there is an eos for the given id in the queue. The queue
 * may contain buckets for the id before that still. */
int h2_bucket_queue_has_eos(h2_bucket_queue *q);

/* Return 1 if the next bucket in the queue is an eos bucket */
int h2_bucket_queue_is_eos(h2_bucket_queue *q);

/* Get the first bucket from the head of the queue. Will
 * return a bucket with APR_SUCCESS, APR_EOF if the stream is closed and
 * APR_EAGAIN if there currently is no bucket.
 */
apr_status_t h2_bucket_queue_pop(h2_bucket_queue *q,
                                 struct h2_bucket **pbucket);

/**
 * Consume the brigade and append up to buf_max length to the queue. 
 * If not all data could be appended, return APR_EINCOMPLETE.
 */
apr_status_t h2_bucket_queue_consume(h2_bucket_queue *q, 
                                     apr_bucket_brigade *bb, 
                                     apr_size_t buf_max);

#endif /* defined(__mod_h2__h2_bucket_queue__) */
