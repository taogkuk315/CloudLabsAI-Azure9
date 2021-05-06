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

#include <assert.h>

#include <httpd.h>
#include <http_core.h>
#include <http_log.h>

#include "h2_private.h"
#include "h2_io.h"
#include "h2_util.h"

static const apr_off_t BLOCKSIZE = 4 * 1024;

int h2_io_init(conn_rec *c, h2_io_ctx *io)
{
    io->connection = c;
    io->input_brigade = apr_brigade_create(c->pool, c->bucket_alloc);
    io->output_brigade = apr_brigade_create(c->pool, c->bucket_alloc);
    return OK;
}

apr_status_t h2_io_bucket_read(apr_bucket_brigade *input,
                               unsigned char *buf, size_t length,
                               size_t *read)
{
    *read = 0;
    while (!APR_BRIGADE_EMPTY(input)) {
        apr_bucket* bucket = APR_BRIGADE_FIRST(input);
        if (APR_BUCKET_IS_METADATA(bucket)) {
            /* we do nothing regarding any meta here */
        }
        else {
            const char *bucket_data = NULL;
            apr_size_t bucket_length = 0;
            apr_status_t rv = apr_bucket_read(bucket, &bucket_data,
                                            &bucket_length, APR_NONBLOCK_READ);
            if (rv != APR_SUCCESS) {
                return rv;
            }
            
            if (bucket_length > 0) {
                if (bucket_length > length) {
                    /* We cannot read more. Split the bucket, copy
                     * the bytes and return.
                     */
                    rv = apr_bucket_split(bucket, length);
                    if (rv != APR_SUCCESS) {
                        return rv;
                    }
                    memcpy(buf, bucket_data, length);
                    *read += length;
                    return APR_SUCCESS;
                }
                else {
                    memcpy(buf, bucket_data, bucket_length);
                    *read += bucket_length;
                }
            }
        }
        apr_bucket_delete(bucket);
    }
    return APR_SUCCESS;
}

apr_status_t h2_io_read(h2_io_ctx *io, unsigned char *buf, size_t length,
                        size_t *read)
{
    apr_status_t status;
    *read = 0;
    if (!APR_BRIGADE_EMPTY(io->input_brigade)) {
        /* Seems something is left from a previous read, lets
         * satisfy our caller with the data we already have. */
        status = h2_io_bucket_read(io->input_brigade, buf, length, read);
        if (status != APR_SUCCESS || *read > 0) {
            return status;
        }
        apr_brigade_cleanup(io->input_brigade);
    }
    
    status = ap_get_brigade(io->connection->input_filters,
                        io->input_brigade, AP_MODE_READBYTES,
                        APR_NONBLOCK_READ, BLOCKSIZE);
    switch (status) {
        case APR_SUCCESS:
            return h2_io_bucket_read(io->input_brigade, buf, length, read);
        case APR_EOF:
            return APR_EOF;
        case APR_EAGAIN:
        case APR_TIMEUP:
            status = h2_io_bucket_read(io->input_brigade, buf, length, read);
            if (status == APR_SUCCESS && *read == 0) {
                return APR_EAGAIN;
            }
            break;
        default:
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, io->connection,
                          "h2_io: error reading");
            break;
    }
    return status;
}

apr_status_t h2_io_write(h2_io_ctx *io, const unsigned char *buf, size_t length,
                         size_t *written)
{
    *written = 0;
    /* we do not want to send something leftover in the brigade */
    assert(APR_BRIGADE_EMPTY(io->output_brigade));
    
    /* Append our data and a flush, since we most likely have a complete
     * frame that must be send now. 
     * TODO: is there a flush indication maybe from higher up???
     */
    APR_BRIGADE_INSERT_TAIL(io->output_brigade,
            apr_bucket_transient_create((const char *)buf, length,
                                        io->output_brigade->bucket_alloc));
    APR_BRIGADE_INSERT_TAIL(io->output_brigade,
            apr_bucket_flush_create(io->output_brigade->bucket_alloc));
    
    /* Send it out through installed filters (TLS) to the client */
    apr_status_t status = ap_pass_brigade(io->connection->output_filters,
                                          io->output_brigade);
    apr_brigade_cleanup(io->output_brigade);
    
    if (status == APR_SUCCESS
        || APR_STATUS_IS_ECONNABORTED(status)
        || APR_STATUS_IS_EPIPE(status)) {
        /* These are all fine and no reason for concern. Everything else
         * is interesting. */
        *written = length;
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, io->connection,
                      "h2_io: write error");
    }

    return status;
}
