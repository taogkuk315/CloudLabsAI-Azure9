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

#ifndef __mod_h2__h2_io__
#define __mod_h2__h2_io__

struct apr_thread_cond_t;
struct h2_bucket;
struct h2_response;

#include "h2_bucket_queue.h"

typedef struct h2_io h2_io;
struct h2_io {
    int id;
    h2_bucket_queue input;
    struct apr_thread_cond_t *input_arrived;

    h2_bucket_queue output;
    struct apr_thread_cond_t *output_drained;

    struct h2_response *response;
};

h2_io *h2_io_create(int id, apr_pool_t *pool);
void h2_io_destroy(h2_io *io);

int h2_io_in_has_eos_for(h2_io *io);
int h2_io_out_has_data(h2_io *io);

apr_status_t h2_io_in_read(h2_io *io, struct h2_bucket **pbucket);
apr_status_t h2_io_in_write(h2_io *io, struct h2_bucket *bucket);
apr_status_t h2_io_in_close(h2_io *io);

apr_status_t h2_io_out_read(h2_io *io, struct h2_bucket **pbucket, int *peos);
apr_status_t h2_io_out_write(h2_io *io, struct h2_bucket *bucket);
apr_status_t h2_io_out_close(h2_io *io);
apr_size_t h2_io_out_length(h2_io *io);


#endif /* defined(__mod_h2__h2_io__) */
