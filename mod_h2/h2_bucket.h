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


#ifndef __mod_h2__h2_bucket__
#define __mod_h2__h2_bucket__

typedef struct h2_bucket h2_bucket;

typedef void h2_bucket_free_func(h2_bucket *bucket);

/* Our own implementation of a data container, invented to 
 * - allocate a fixed, single memory chunk per bucket which
 *   keeps struct and data itself.
 * - has utility functions for appending inside the bucket etc.
 * - can serve as a scratch buffer inside a single thread
 * - can, once constructed, be passed around safely
 * - can be destroyed, e.g. freed, without knowledge how it was allocated
 */
struct h2_bucket {
    char *data;
    apr_size_t data_len;
    apr_size_t data_size;
    h2_bucket_free_func *free_bucket;
};

/* Singular instance, useful in indicating end-of-stream or such */
extern h2_bucket H2_NULL_BUCKET;

/* Allocate a bucket from heap, will free memory when destroyed */
h2_bucket *h2_bucket_alloc(apr_size_t data_size);

/* Destroy the bucket and release memory when possible */
void h2_bucket_destroy(h2_bucket *bucket);

/* Append data to the bucket and return the number of bytes placed
 * into the bucket. This will be less or even 0 if the bucket has no
 * more room. 
 * Use this only from one thread. */
apr_size_t h2_bucket_append(h2_bucket *bucket,
                            const char *data, apr_size_t len);
/* Append the null-terminated string to the bucket. Return the
 * number of bytes copied. Will not copy the terminating null.
 * Use this only from one thread. */
apr_size_t h2_bucket_cat(h2_bucket *bucket, const char *s);

/* Return != 0, iff the given number if bytes are available. */
int h2_bucket_has_free(h2_bucket *bucket, size_t bytes);

/* Return the number of bytes available. */
apr_size_t h2_bucket_available(h2_bucket *bucket);

/* Reset the bucket to be completely free again. */
void h2_bucket_reset(h2_bucket *bucket);

/* Copy data from the bucket into the given buffer. Return the
 * number of bytes copied. */
apr_size_t h2_bucket_copy(const h2_bucket *bucket, char *buf, apr_size_t len);

/* Move data from the bucket into the given buffer. Return the
 * number of bytes moved. The bucket will be modified by this operation
 * and:
 * - will be empty when all remaining data could be moved
 * - will keep the data that had not been moved due to length
 *   limitations in the target buffer
 */
apr_size_t h2_bucket_move(h2_bucket *bucket, char *buf, apr_size_t len);

#endif /* defined(__mod_h2__h2_bucket__) */
