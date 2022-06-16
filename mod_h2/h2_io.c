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
        h2_bucket_queue_init(&io->input);
        io->bbout = apr_brigade_create(pool, bucket_alloc);
    }
    return io;
}

void h2_io_cleanup(h2_io *io)
{
    h2_bucket_queue_cleanup(&io->input);
    if (io->response) {
        h2_response_destroy(io->response);
        io->response = NULL;
    }
}

void h2_io_destroy(h2_io *io)
{
    h2_io_cleanup(io);
    apr_brigade_destroy(io->bbout);
}

int h2_io_in_has_eos_for(h2_io *io)
{
    return h2_bucket_queue_has_eos(&io->input);
}

int h2_io_out_has_data(h2_io *io)
{
    return !APR_BRIGADE_EMPTY(io->bbout);
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

apr_status_t h2_io_in_write(h2_io *io, struct h2_bucket *bucket)
{
    return h2_bucket_queue_append(&io->input, bucket);
}

apr_status_t h2_io_in_close(h2_io *io)
{
    return h2_bucket_queue_append_eos(&io->input);
}

h2_response *h2_io_extract_response(h2_io *io)
{
    return io->response;
}


apr_status_t h2_io_out_read(h2_io *io, apr_bucket_brigade *bb, 
                            apr_size_t maxlen)
{
    return h2_util_move(bb, io->bbout, maxlen, 0, "h2_io_out_read");
}

apr_status_t h2_io_out_write(h2_io *io, apr_bucket_brigade *bb, 
                             apr_size_t maxlen)
{
    return h2_util_move(io->bbout, bb, maxlen, 0, "h2_io_out_write");
}


apr_status_t h2_io_out_close(h2_io *io)
{
    APR_BRIGADE_INSERT_TAIL(io->bbout, 
                            apr_bucket_eos_create(io->bbout->bucket_alloc));
    return APR_SUCCESS;
}
