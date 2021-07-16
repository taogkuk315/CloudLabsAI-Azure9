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

/**
 * A h2_task represents a faked HTTP/1.1 request from the client that
 * is created for every HTTP/2 stream (HEADER+CONT.+DATA) we receive
 * from the client.
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
 * h2_task input/output are the h2_bucket_queue pairs of the h2_session this
 * task belongs to. h2_task_input and h2_task_output convert this into/from
 * proper apr_bucket_brigadedness.
 *
 * Finally, to keep certain connection level filters, such as ourselves and
 * especially mod_ssl ones, from messing with our data, we need a filter
 * of our own to disble those.
 */

struct h2_mplx;
struct h2_task;
struct h2_resp_head;
struct h2_response;
struct h2_bucket;
struct h2_bucket_queue;

typedef enum {
    H2_TASK_ST_IDLE,
    H2_TASK_ST_STARTED,
    H2_TASK_ST_READY,
    H2_TASK_ST_DONE
} h2_task_state_t;

typedef struct h2_task h2_task;

h2_task *h2_task_create(long session_id,
                        int stream_id,
                        conn_rec *master,
                        struct h2_bucket *input,
                        struct h2_mplx *mplx);

apr_status_t h2_task_destroy(h2_task *task);

apr_status_t h2_task_do(h2_task *task);

void h2_task_abort(h2_task *task);

long h2_task_get_session_id(h2_task *task);
int h2_task_get_stream_id(h2_task *task);

struct h2_resp_head *h2_task_get_resp_head(h2_task *task);

void h2_task_register_hooks(void);

int h2_task_pre_conn(h2_task *task, conn_rec *c);

#endif /* defined(__mod_h2__h2_task__) */
