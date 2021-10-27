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

#ifndef __mod_h2__h2_conn__
#define __mod_h2__h2_conn__

struct h2_task;

/* Process the connection that is now starting the HTTP/2
 * conversation. Return when the HTTP/2 session is done
 * and the connection will close.
 */
apr_status_t h2_conn_main(conn_rec *c);

/* Process the request that has been upgraded to a HTTP/2
 * conversation. Return when the HTTP/2 session is done
 * and the connection will close.
 */
apr_status_t h2_conn_rprocess(request_rec *r);

/* Initialize this child process for h2 connection work,
 * to be called once during child init before multi processing
 * starts.
 */
apr_status_t h2_conn_child_init(apr_pool_t *pool, server_rec *s);


typedef enum {
    H2_MPM_UNKNOWN,
    H2_MPM_WORKER,
    H2_MPM_EVENT,
} h2_mpm_type_t;

/* Returns the type of MPM module detected */
h2_mpm_type_t h2_conn_mpm_type();

/* Gives the detected module itself or NULL if unknown */
module *h2_conn_mpm_module();


typedef struct h2_conn h2_conn;
struct h2_conn {
    const char *id;
    apr_pool_t *pool;
    apr_bucket_alloc_t *bucket_alloc;
    conn_rec *c;
    apr_socket_t *socket;
    conn_rec *master;
};

h2_conn *h2_conn_create(const char *id, conn_rec *master, apr_pool_t *parent);

void h2_conn_destroy(h2_conn *conn);

apr_status_t h2_conn_prep(h2_conn *conn, apr_thread_t *thd);

apr_status_t h2_conn_process(h2_conn *conn);

#endif /* defined(__mod_h2__h2_conn__) */
