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
#include <http_config.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_frame.h"

#define HTTP_RLINE_SUFFIX       " HTTP/1.1\r\n"
#define HTTP_RLINE_SUFFIX_LEN   11

apr_status_t h2_frame_req_add_start(h2_bucket *bucket,
                                    const char *method, const char *path)
{
    size_t mlen = strlen(method);
    size_t plen = strlen(path);
    size_t total = mlen + 1 + plen + HTTP_RLINE_SUFFIX_LEN;
    if (!h2_bucket_has_free(bucket, total)) {
        return APR_ENAMETOOLONG;
    }
    h2_bucket_append(bucket, method, mlen);
    h2_bucket_append(bucket, " ", 1);
    h2_bucket_append(bucket, path, plen);
    h2_bucket_append(bucket, HTTP_RLINE_SUFFIX, HTTP_RLINE_SUFFIX_LEN);
    return APR_SUCCESS;
}

apr_status_t h2_frame_req_add_header(h2_bucket *bucket,
                                     const char *name, size_t nlen,
                                     const char *value, size_t vlen)
{
    if (nlen > 0) {
        size_t total = nlen + vlen + 4;
        if (!h2_bucket_has_free(bucket, total)) {
            return APR_ENAMETOOLONG;
        }
        h2_bucket_append(bucket, name, nlen);
        h2_bucket_append(bucket, ": ", 2);
        if (vlen > 0) {
            h2_bucket_append(bucket, value, vlen);
        }
        h2_bucket_append(bucket, "\r\n", 2);
    }
    return APR_SUCCESS;
}

