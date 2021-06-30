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


#ifndef __mod_h2__h2_mplx__
#define __mod_h2__h2_mplx__

struct apr_pool_t;
struct apr_thread_mutex_t;
struct apr_thread_cond_t;
struct h2_bucket;
struct h2_bucket_queue;
struct h2_resp_head;

struct h2_mplx *h2_mplx_create(conn_rec *c);
void h2_mplx_destroy(struct h2_mplx *mplx);

apr_status_t h2_mplx_reference(struct h2_mplx *mplx);
apr_status_t h2_mplx_release(struct h2_mplx *mplx);

void h2_mplx_abort(struct h2_mplx *mplx);

apr_status_t h2_mplx_in_read(struct h2_mplx *mplx, apr_read_type_e block,
                             int channel, struct h2_bucket **pbucket);

apr_status_t h2_mplx_in_write(struct h2_mplx *mplx,
                              int channel, struct h2_bucket *bucket);

apr_status_t h2_mplx_in_close(struct h2_mplx *m, int channel);

int h2_mplx_in_has_eos_for(struct h2_mplx *m, int channel);

apr_status_t h2_mplx_out_read(struct h2_mplx *mplx,
                              int channel, struct h2_bucket **pbucket);
apr_status_t h2_mplx_out_pushback(struct h2_mplx *mplx, int channel,
                                  struct h2_bucket *bucket);

apr_status_t h2_mplx_out_open(struct h2_mplx *mplx, int channel,
                              struct h2_resp_head *head);

apr_status_t h2_mplx_out_write(struct h2_mplx *mplx, apr_read_type_e block,
                               int channel, struct h2_bucket *bucket);

apr_status_t h2_mplx_out_close(struct h2_mplx *m, int channel);

apr_status_t h2_mplx_out_trywait(struct h2_mplx *m, apr_interval_time_t timeout);

int h2_mplx_out_has_data_for(struct h2_mplx *m, int channel);

struct h2_resp_head *h2_mplx_pop_response(struct h2_mplx *m);

#endif /* defined(__mod_h2__h2_mplx__) */
