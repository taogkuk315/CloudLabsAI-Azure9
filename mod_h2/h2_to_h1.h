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

#ifndef __mod_h2__h2_to_h1__
#define __mod_h2__h2_to_h1__

struct h2_mplx;
typedef struct h2_to_h1 h2_to_h1;

h2_to_h1 *h2_to_h1_create(apr_pool_t *pool);

void h2_to_h1_destroy(h2_to_h1 *to_h1);

apr_status_t h2_to_h1_start_request(h2_to_h1 *to_h1, int stream_id, 
                                    const char *method, const char *path,
                                    const char *authority, struct h2_mplx *m);

apr_status_t h2_to_h1_add_header(h2_to_h1 *to_h1,
                                 const char *name, size_t nlen,
                                 const char *value, size_t vlen,
                                 struct h2_mplx *m);

apr_status_t h2_to_h1_end_headers(h2_to_h1 *to_h1, struct h2_mplx *m);

apr_status_t h2_to_h1_add_data(h2_to_h1 *to_h1,
                               const char *data, size_t len,
                               struct h2_mplx *m);

apr_status_t h2_to_h1_flush(h2_to_h1 *to_h1, struct h2_mplx *m);
apr_status_t h2_to_h1_close(h2_to_h1 *to_h1, struct h2_mplx *m);

h2_bucket *h2_to_h1_steal_first_data(h2_to_h1 *to_h1, int *peos);

#endif /* defined(__mod_h2__h2_to_h1__) */
