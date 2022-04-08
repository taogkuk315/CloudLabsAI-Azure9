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

#ifndef __mod_h2__h2_to_h1__
#define __mod_h2__h2_to_h1__

struct h2_bucket_queue;

typedef struct h2_to_h1 h2_to_h1;

/* Create a converter from a HTTP/2 request to a serialzation in
 * HTTP/1.1 format. The serialized data will be written onto the
 * given h2_mplx instance.
 */
h2_to_h1 *h2_to_h1_create(int stream_id, apr_pool_t *pool, 
                          struct h2_bucket_queue *bq);

/* Destroy the converter and free resources. */
void h2_to_h1_destroy(h2_to_h1 *to_h1);

/* Start a request with the given method, path and optional authority. For
 * traceablility reasons, the stream identifier is also given.
 */
apr_status_t h2_to_h1_start_request(h2_to_h1 *to_h1, int stream_id, 
                                    const char *method, const char *path,
                                    const char *authority);

/* Add a header to the serialization. Only valid to call after start
 * and before end_headers.
 */
apr_status_t h2_to_h1_add_header(h2_to_h1 *to_h1,
                                 const char *name, size_t nlen,
                                 const char *value, size_t vlen);

/* End the request headers.
 */
apr_status_t h2_to_h1_end_headers(h2_to_h1 *to_h1);

/* Add request body data.
 */
apr_status_t h2_to_h1_add_data(h2_to_h1 *to_h1,
                               const char *data, size_t len);

/* Flush the converted data onto the h2_mplx instance.
 */
apr_status_t h2_to_h1_flush(h2_to_h1 *to_h1);

/* Close the request, flushed automatically.
 */
apr_status_t h2_to_h1_close(h2_to_h1 *to_h1);

#endif /* defined(__mod_h2__h2_to_h1__) */
