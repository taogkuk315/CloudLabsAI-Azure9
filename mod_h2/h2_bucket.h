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

struct h2_bucket;

typedef void h2_bucket_free_func(struct h2_bucket *bucket);

typedef struct h2_bucket {
    char *data;
    apr_size_t data_len;
    apr_size_t data_size;
    h2_bucket_free_func *free_bucket;
} h2_bucket;

h2_bucket *h2_bucket_alloc(apr_size_t data_size);
void h2_bucket_destroy(h2_bucket *bucket);

apr_size_t h2_bucket_append(h2_bucket *bucket,
                            const char *data, apr_size_t len);
apr_size_t h2_bucket_cat(h2_bucket *bucket, const char *s);

int h2_bucket_has_free(h2_bucket *bucket, size_t bytes);


#endif /* defined(__mod_h2__h2_bucket__) */
