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

#include <assert.h>

#include <apr_thread_cond.h>

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>
#include <http_connection.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_bucket_queue.h"
#include "h2_io.h"
#include "h2_response.h"
#include "h2_util.h"

h2_io *h2_io_create(int id, apr_pool_t *pool, apr_bucket_alloc_t *bucket_alloc)
{
    h2_io *io = apr_pcalloc(pool, sizeof(*io));
    if (io) {
        io->id = id;
        io->pool = pool;
        h2_bucket_queue_init(&io->input);
        io->bbout = apr_brigade_create(pool, bucket_alloc);
    }
    return io;
}

void h2_io_cleanup(h2_io *io)
{
    h2_bucket_queue_cleanup(&io->input);
}

void h2_io_destroy(h2_io *io)
{
    h2_io_cleanup(io);
    io->bbout = NULL;
}

apr_size_t h2_io_out_length(h2_io *io)
{
    if (io->bbout) {
        apr_off_t len = 0;
        apr_brigade_length(io->bbout, 0, &len);
        return (len > 0)? len : 0;
    }
    return 0;
}

apr_status_t h2_io_in_read(h2_io *io, struct h2_bucket **pbucket)
{
    apr_status_t status = h2_bucket_queue_pop(&io->input, pbucket);
    if (status == APR_SUCCESS) {
        io->input_consumed += (*pbucket)->data_len;
    }
    return status;
}

h2_response *h2_io_extract_response(h2_io *io)
{
    h2_response *resp = NULL;
    if (io->response) {
        resp = io->response;
        io->response = NULL;
    }
    return resp;
}


apr_status_t h2_io_out_write(h2_io *io, apr_bucket_brigade *bb, 
                             apr_size_t maxlen)
{
    return h2_util_move(io->bbout, bb, maxlen);
}


apr_status_t h2_io_out_close(h2_io *io)
{
    APR_BRIGADE_INSERT_TAIL(io->bbout, 
                            apr_bucket_eos_create(io->bbout->bucket_alloc));
    return APR_SUCCESS;
}

apr_status_t h2_io_sync(h2_io *io, struct h2_bucket_queue *input, 
                        apr_bucket_brigade *output)
{
    apr_status_t status = APR_EAGAIN;
    if (!H2_QUEUE_EMPTY(input)) {
        H2_QUEUE_CONCAT(&io->input, input);
        status = APR_SUCCESS;
        if (io->input_arrived) {
            apr_thread_cond_signal(io->input_arrived);
        }
    }
    if (!APR_BRIGADE_EMPTY(io->bbout)) {
        status = h2_util_move(output, io->bbout, 0);
        if (io->output_drained) {
            apr_thread_cond_signal(io->output_drained);
        }
    }
    return status;
}
