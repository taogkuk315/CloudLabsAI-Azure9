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

#ifndef __mod_h2__h2_ctx__
#define __mod_h2__h2_ctx__

typedef struct {
    int is_h2;
    const char *protocol;
    int is_slave;
    int is_negotiated;
    
} h2_ctx;

h2_ctx *h2_ctx_create(conn_rec *c);
h2_ctx *h2_ctx_get(conn_rec *c);

const char *h2_ctx_get_protocol(conn_rec* c);
h2_ctx *h2_ctx_set_protocol(conn_rec* c, const char *proto);
int h2_ctx_is_negotiated(conn_rec * c);

int h2_ctx_is_master(conn_rec * c);
int h2_ctx_is_slave(conn_rec * c);
int h2_ctx_is_active(conn_rec * c);

#endif /* defined(__mod_h2__h2_ctx__) */
