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


#ifndef __mod_h2__h2_h2__
#define __mod_h2__h2_h2__

/* Implementation of the "h2" specific parts for enabling HTTP2
 * over a TLS connection.
 */

/*
 * One time, post config intialization.
 */
apr_status_t h2_h2_init(apr_pool_t *pool, server_rec *s);

/*
 * Once per child process initialization.
 */
apr_status_t h2_h2_child_init(apr_pool_t *pool, server_rec *s);

/* Hooks for processing incoming connections:
 * - pre_conn resgiters for NPN/ALPN handling
 * - process_conn takes of the connection instead of core should "h2"
 *            have been selected
 * - stream_pre_conn disables mod_ssl connection filters for our
 *            stream pseudo connections
 */
int h2_h2_pre_conn(conn_rec* c, void *arg);
int h2_h2_process_conn(conn_rec* c);
int h2_h2_stream_pre_conn(conn_rec* c, void *arg);

/* Is the connection a TLS connection?
 */
int h2_h2_is_tls(conn_rec *c);

/* Register apache hooks for h2 protocol
 */
void h2_h2_register_hooks(void);


#endif /* defined(__mod_h2__h2_h2__) */
