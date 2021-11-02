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
#include "h2_queue.h"
#include "h2_bucket_queue.h"
#include "h2_io.h"

h2_io *h2_io_create(int id, apr_pool_t *pool)
{
    h2_io *io = apr_pcalloc(pool, sizeof(*io));
    if (io) {
        io->id = id;
        io->input = h2_bucket_queue_create(pool);
        io->output = h2_bucket_queue_create(pool);
    }
    return io;
}

void h2_io_destroy(h2_io *io)
{
    if (io->input) {
        h2_bucket_queue_destroy(io->input);
        io->input = NULL;
    }
    if (io->output) {
        h2_bucket_queue_destroy(io->output);
        io->output = NULL;
    }
}

int h2_io_in_has_eos_for(h2_io *io)
{
    return h2_bucket_queue_has_eos_for(io->input, io->id);
}

int h2_io_out_has_data(h2_io *io)
{
    return h2_bucket_queue_has_buckets_for(io->output, io->id);
}

apr_size_t h2_io_out_length(h2_io *io)
{
    return h2_bucket_queue_get_stream_size(io->output, io->id);
}

apr_status_t h2_io_in_read(h2_io *io, struct h2_bucket **pbucket)
{
    return h2_bucket_queue_pop(io->input, io->id, pbucket);
}

apr_status_t h2_io_in_write(h2_io *io, struct h2_bucket *bucket)
{
    return h2_bucket_queue_append(io->input, io->id, bucket);
}

apr_status_t h2_io_in_close(h2_io *io)
{
    return h2_bucket_queue_append_eos(io->input, io->id);
}

apr_status_t h2_io_out_read(h2_io *io, struct h2_bucket **pbucket)
{
    return h2_bucket_queue_pop(io->output, io->id, pbucket);
}
    
apr_status_t h2_io_out_pushback(h2_io *io, struct h2_bucket *bucket)
{
    return h2_bucket_queue_push(io->output, io->id, bucket);
}

apr_status_t h2_io_out_write(h2_io *io, struct h2_bucket *bucket)
{
    return h2_bucket_queue_append(io->output, io->id, bucket);
}

apr_status_t h2_io_out_close(h2_io *io)
{
    return h2_bucket_queue_append_eos(io->output, io->id);
}
