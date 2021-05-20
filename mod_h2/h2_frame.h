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

#ifndef __mod_h2__h2_frame__
#define __mod_h2__h2_frame__

#include "h2_bucket.h"

apr_status_t h2_frame_req_add_start(h2_bucket *bucket,
                                    const char *method, const char *path);

apr_status_t h2_frame_req_add_header(h2_bucket *bucket,
                                     const char *name, size_t nlen,
                                     const char *value, size_t vlen);

#endif /* defined(__mod_h2__h2_frame__) */
