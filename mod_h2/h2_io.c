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
