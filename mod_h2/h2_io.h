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

#ifndef __mod_h2__h2_io__
#define __mod_h2__h2_io__

typedef struct {
    conn_rec *connection;
    apr_bucket_brigade *input_brigade;
    apr_bucket_brigade *output_brigade;
} h2_io_ctx;

int h2_io_init(conn_rec *c, h2_io_ctx *io);

typedef apr_status_t (*h2_io_on_read_cb)(const char *data, apr_size_t len,
                                         apr_size_t *readlen, int *done,
                                         void *puser);


apr_status_t h2_io_read(h2_io_ctx *io,
                        apr_read_type_e block,
                        h2_io_on_read_cb on_read_cb,
                        void *puser);

apr_status_t h2_io_read_copy(h2_io_ctx *io,
                             apr_read_type_e block,
                             char *buf, size_t length,
                             size_t *read);

apr_status_t h2_io_write(h2_io_ctx *io,
                         const char *buf,
                         size_t length,
                         size_t *written);

#endif /* defined(__mod_h2__h2_io__) */
