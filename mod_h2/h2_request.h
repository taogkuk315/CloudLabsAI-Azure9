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

#ifndef __mod_h2__h2_request__
#define __mod_h2__h2_request__

typedef struct h2_mplx h2_mplx;
typedef struct h2_request h2_request;

h2_request *h2_request_create(apr_pool_t *pool, int id);

void h2_request_destroy(h2_request *req);

apr_status_t h2_request_write_header(h2_request *req,
                                     const char *name, size_t nlen,
                                     const char *value, size_t vlen,
                                     h2_mplx *m);


apr_status_t h2_request_write_data(h2_request *request,
                                   const char *data, size_t len,
                                   h2_mplx *m);

apr_status_t h2_request_end_headers(h2_request *req, struct h2_mplx *m);
apr_status_t h2_request_close(h2_request *req, h2_mplx *m);

#endif /* defined(__mod_h2__h2_request__) */
