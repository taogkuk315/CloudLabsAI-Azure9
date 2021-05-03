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


#ifndef __mod_h2__h2_tls__
#define __mod_h2__h2_tls__

/**
 * One time, post config intialization.
 */
void h2_tls_init(apr_pool_t *pool, server_rec *s);

/**
 * Once per child process initialization.
 */
void h2_tls_child_init(apr_pool_t *pool, server_rec *s);

/**
 * hooks for processing incoming connections.
 */
int h2_tls_pre_conn(conn_rec* c, void *arg);
int h2_tls_process_conn(conn_rec* c);

/**
 * Is the connection a TLS connection?
 */
int h2_tls_is_tls(conn_rec *c);


#endif /* defined(__mod_h2__h2_tls__) */
