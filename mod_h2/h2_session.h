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


#ifndef __mod_h2__h2_session__
#define __mod_h2__h2_session__

#include <nghttp2/nghttp2.h>

#include "h2_io.h"

typedef struct h2_session {
    conn_rec *connection;
    nghttp2_session *session;
    h2_io_ctx io;
} h2_session;

apr_status_t h2_session_serve(conn_rec *c);

#endif /* defined(__mod_h2__h2_session__) */
