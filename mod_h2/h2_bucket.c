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
#include <http_connection.h>

#include "h2_private.h"
#include "h2_bucket.h"

h2_bucket H2_NULL_BUCKET = { NULL, 0, 0, NULL };

static void bucket_free(h2_bucket *bucket)
{
    free(bucket);
}

h2_bucket *h2_bucket_alloc(apr_size_t data_size)
{
    apr_size_t total = sizeof(h2_bucket) + data_size;
    h2_bucket *bucket = calloc(total, sizeof(char));
    if (bucket != NULL) {
        bucket->data = ((char *)bucket) + sizeof(h2_bucket);
        bucket->data_size = data_size;
        bucket->free_bucket = bucket_free;
    }
    return bucket;
}

void h2_bucket_destroy(h2_bucket *bucket)
{
    if (bucket->free_bucket) {
        bucket->free_bucket(bucket);
    }
}

apr_size_t h2_bucket_append(h2_bucket *bucket,
                            const char *data, apr_size_t len)
{
    assert(bucket);
    assert(bucket->data_size >= bucket->data_len);
    apr_size_t free_len = bucket->data_size - bucket->data_len;
    if (len > free_len) {
        len = free_len;
    }
    if (len > 0) {
        memcpy(bucket->data + bucket->data_len, data, len);
        bucket->data_len += len;
    }
    return len;
}

apr_size_t h2_bucket_cat(h2_bucket *bucket, const char *s)
{
    return h2_bucket_append(bucket, (const char *)s, strlen(s));
}

int h2_bucket_has_free(h2_bucket *bucket, size_t bytes)
{
    return bytes <= h2_bucket_available(bucket);
}

apr_size_t h2_bucket_available(h2_bucket *bucket)
{
    if (bucket->data_size > bucket->data_len) {
        return bucket->data_size - bucket->data_len;
    }
    return 0;
}

void h2_bucket_reset(h2_bucket *bucket)
{
    bucket->data_len = 0;
    memset(bucket->data, 0, bucket->data_size);
}

apr_size_t h2_bucket_copy(const h2_bucket *bucket, char *buf, apr_size_t len)
{
    apr_size_t copied = (len > bucket->data_len)? bucket->data_len : len;
    memcpy(buf, bucket->data, copied);
    return copied;
}

apr_size_t h2_bucket_move(h2_bucket *bucket, char *buf, apr_size_t len)
{
    /* copy as much data as we can and update the bucket to show
     * any data that has not been copied yet. */
    apr_size_t copied = h2_bucket_copy(bucket, buf, len);
    if (copied > 0) {
        assert(copied <= bucket->data_len);
        bucket->data_len -= copied;
        if (bucket->data_len > 0) {
            bucket->data += copied;
        }
    }
    return copied;
}


