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
#include "h2_io.h"
#include "h2_response.h"
#include "h2_util.h"

h2_io *h2_io_create(int id, apr_pool_t *pool, apr_bucket_alloc_t *bucket_alloc)
{
    h2_io *io = apr_pcalloc(pool, sizeof(*io));
    if (io) {
        io->id = id;
        io->bbin = apr_brigade_create(pool, bucket_alloc);
        io->bbout = apr_brigade_create(pool, bucket_alloc);
    }
    return io;
}

void h2_io_cleanup(h2_io *io)
{
    h2_response_cleanup(&io->response);
    if (io->file) {
        ap_log_perror(APLOG_MARK, APLOG_TRACE1, 0, io->bbout->p,
                      "h2_io(%d): cleanup, closing file", io->id);
        apr_file_close(io->file);
        io->file = NULL;
    }
}

void h2_io_destroy(h2_io *io)
{
    h2_io_cleanup(io);
}

int h2_io_in_has_eos_for(h2_io *io)
{
    return h2_util_has_eos(io->bbin, 0);
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

apr_status_t h2_io_in_read(h2_io *io, apr_bucket_brigade *bb, 
                           apr_size_t maxlen)
{
    apr_off_t start_len = 0;

    if (APR_BRIGADE_EMPTY(io->bbin)) {
        return io->eos_in? APR_EOF : APR_EAGAIN;
    }
    
    apr_brigade_length(bb, 1, &start_len);
    apr_bucket *last = APR_BRIGADE_LAST(bb);
    apr_status_t status = h2_util_move(bb, io->bbin, maxlen, 0, 
                                       NULL, "h2_io_in_read");
    if (status == APR_SUCCESS) {
        apr_bucket *nlast = APR_BRIGADE_LAST(bb);
        apr_off_t end_len = 0;
        apr_brigade_length(bb, 1, &end_len);
        if (last == nlast) {
            return APR_EAGAIN;
        }
        io->input_consumed += (end_len - start_len);
    }
    return status;
}

apr_status_t h2_io_in_write(h2_io *io, apr_bucket_brigade *bb)
{
    if (io->eos_in) {
        return APR_EOF;
    }
    io->eos_in = h2_util_has_eos(bb, 0);
    return h2_util_move(io->bbin, bb, 0, 0, NULL, "h2_io_in_write");
}

apr_status_t h2_io_in_close(h2_io *io)
{
    APR_BRIGADE_INSERT_TAIL(io->bbin, 
                            apr_bucket_eos_create(io->bbin->bucket_alloc));
    io->eos_in = 1;
    return APR_SUCCESS;
}

apr_status_t h2_io_out_read(h2_io *io, char *buffer, 
                            apr_size_t *plen, int *peos)
{
    apr_status_t status = APR_SUCCESS;
    apr_size_t avail = *plen;
    apr_size_t written = 0;
    apr_bucket *b;
    
    if (buffer == NULL) {
        /* test read to determine available length */
        apr_off_t blen = 0;
        status = apr_brigade_length(io->bbout, 0, &blen);
        if (blen < *plen) {
            *plen = blen;
        }
        *peos = h2_util_has_eos(io->bbout, *plen);
        return status;
    }
    
    /* Copy data in our brigade into the buffer until it is filled or
     * we encounter an EOS.
     */
    while ((status == APR_SUCCESS) 
           && !APR_BRIGADE_EMPTY(io->bbout)
           && (avail > 0)) {
        
        apr_bucket *b = APR_BRIGADE_FIRST(io->bbout);
        if (APR_BUCKET_IS_METADATA(b)) {
            if (APR_BUCKET_IS_EOS(b)) {
                *peos = 1;
            }
            else {
                /* ignore */
            }
        }
        else {
            const char *data;
            apr_size_t data_len;

            if (0 && APR_BUCKET_IS_FILE(b)) {
                ap_log_perror(APLOG_MARK, APLOG_NOTICE, 0, io->bbout->p,
                              "h2_io(%d): reading from file(len=%ld) %ld bytes", 
                              io->id, (long)b->length, (long)avail);
            }
            if (b->length != -1 && b->length > avail) {
                apr_bucket_split(b, avail);
            }
            status = apr_bucket_read(b, &data, &data_len, 
                                     APR_NONBLOCK_READ);
            if (status == APR_SUCCESS && data_len > 0) {
                if (data_len > avail) {
                    apr_bucket_split(b, avail);
                    data_len = avail;
                }
                memcpy(buffer, data, data_len);
                avail -= data_len;
                buffer += data_len;
                written += data_len;
            }
        }
        apr_bucket_delete(b);
    }
    
    *plen = written;
    return status;
}

apr_status_t h2_io_out_write(h2_io *io, apr_bucket_brigade *bb, 
                             apr_size_t maxlen)
{
    return h2_util_move(io->bbout, bb, maxlen, 0, &io->file,
                        "h2_io_out_write");
}


apr_status_t h2_io_out_close(h2_io *io)
{
    APR_BRIGADE_INSERT_TAIL(io->bbout, 
                            apr_bucket_eos_create(io->bbout->bucket_alloc));
    return APR_SUCCESS;
}
