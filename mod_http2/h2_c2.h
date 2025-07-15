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

#ifndef __mod_h2__h2_task__
#define __mod_h2__h2_task__

#include <http_core.h>

/**
 * A h2_task fakes a HTTP/1.1 request from the data in a HTTP/2 stream 
 * (HEADER+CONT.+DATA) the module receives.
 *
 * In order to answer a HTTP/2 stream, we want all Apache httpd infrastructure
 * to be involved as usual, as if this stream can as a separate HTTP/1.1
 * request. The basic trickery to do so was derived from google's mod_spdy
 * source. Basically, we fake a new conn_rec object, even with its own
 * socket and give it to ap_process_connection().
 *
 * Since h2_task instances are executed in separate threads, we may have
 * different lifetimes than our h2_stream or h2_session instances. Basically,
 * we would like to be as standalone as possible.
 *
 * Finally, to keep certain connection level filters, such as ourselves and
 * especially mod_ssl ones, from messing with our data, we need a filter
 * of our own to disable those.
 */

/**
 * Process a secondary connection for a HTTP/2 stream request.
 */
apr_status_t h2_c2_process(conn_rec *c, apr_thread_t *thread, int worker_id);

void h2_c2_register_hooks(void);
/*
 * One time, post config initialization.
 */
apr_status_t h2_c2_init(apr_pool_t *pool, server_rec *s);

extern APR_OPTIONAL_FN_TYPE(ap_logio_add_bytes_in) *h2_c2_logio_add_bytes_in;
extern APR_OPTIONAL_FN_TYPE(ap_logio_add_bytes_out) *h2_c2_logio_add_bytes_out;

#endif /* defined(__mod_h2__h2_task__) */
