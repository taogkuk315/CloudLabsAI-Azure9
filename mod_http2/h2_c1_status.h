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

#ifndef __mod_h2__h2_c1_status__
#define __mod_h2__h2_c1_status__

/**
 * The request handler for  /.well-known/h2/state
 * This gives a JSON representation of the HTTP/2 state of the primary connection.
 */
int h2_c1_status_handler(request_rec *r);

/**
 * The status handler internally uses a special bucket type
 * to convert itself to the actual response body before being
 * sent out on the primary connection.
 * This callback makes this bucket type traverse a b2_bucket_beam.
 */
apr_bucket *h2_bucket_observer_beam(struct h2_bucket_beam *beam,
                                    apr_bucket_brigade *dest,
                                    const apr_bucket *src);

#endif /* __mod_h2__h2_c1_status__ */
