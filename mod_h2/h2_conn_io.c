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

#include "h2_private.h"
#include "h2_conn_io.h"
#include "h2_util.h"

static const char HTTP2_PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
static const int HTTP2_PREFACE_LEN = sizeof(HTTP2_PREFACE) - 1;

apr_status_t h2_conn_io_init(h2_conn_io_ctx *io, conn_rec *c, int check_preface)
{
    io->connection = c;
    io->check_preface = check_preface;
    io->preface_bytes_left = check_preface? HTTP2_PREFACE_LEN : 0;
    io->input = apr_brigade_create(c->pool, c->bucket_alloc);
    io->output = apr_brigade_create(c->pool, c->bucket_alloc);
    return APR_SUCCESS;
}

void h2_conn_io_destroy(h2_conn_io_ctx *io)
{
    if (io->input) {
        apr_brigade_destroy(io->input);
        io->input = NULL;
    }
    if (io->output) {
        apr_brigade_destroy(io->output);
        io->output = NULL;
    }
}

static apr_status_t h2_conn_io_bucket_read(h2_conn_io_ctx *io,
                                      apr_read_type_e block,
                                      h2_conn_io_on_read_cb on_read_cb,
                                      void *puser, int *pdone)
{
    apr_status_t status = APR_SUCCESS;
    apr_size_t readlen = 0;
    *pdone = 0;
    
    while (status == APR_SUCCESS && !*pdone
           && !APR_BRIGADE_EMPTY(io->input)) {
        
        apr_bucket* bucket = APR_BRIGADE_FIRST(io->input);
        if (APR_BUCKET_IS_METADATA(bucket)) {
            /* we do nothing regarding any meta here */
        }
        else {
            const char *bucket_data = NULL;
            apr_size_t bucket_length = 0;
            status = apr_bucket_read(bucket, &bucket_data,
                                     &bucket_length, block);
            
            if (status == APR_SUCCESS && bucket_length > 0) {
                if (APLOGctrace2(io->connection)) {
                    char buffer[32];
                    h2_util_hex_dump(buffer, sizeof(buffer)/sizeof(buffer[0]),
                                     bucket_data, bucket_length);
                    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, io->connection,
                                  "h2_conn_io(%ld): read %ld bytes: %s",
                                  io->connection->id, bucket_length, buffer);
                }
                
                if (io->preface_bytes_left > 0) {
                    /* still requiring bytes from the http/2 preface */
                    size_t pre_offset = HTTP2_PREFACE_LEN - io->preface_bytes_left;
                    int check_len = io->preface_bytes_left;
                    if (check_len > bucket_length) {
                        check_len = bucket_length;
                    }
                        
                    if (strncmp(HTTP2_PREFACE+pre_offset, bucket_data, 
                                check_len)) {
                        /* preface mismatch */
                        ap_log_cerror(APLOG_MARK, APLOG_INFO, APR_EMISMATCH, 
                                      io->connection,
                                      "h2_conn_io(%ld): preface check",
                                      io->connection->id);
                        return APR_EMISMATCH;
                    }
                    io->preface_bytes_left -= check_len;
                    bucket_data += check_len;
                    bucket_length -= check_len;
                    ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, io->connection,
                                  "h2_conn_io(%ld): preface check: %d bytes "
                                  "matched, remaining %d",
                                  io->connection->id, check_len, 
                                  io->preface_bytes_left);
                }
                
                if (bucket_length > 0) {
                    apr_size_t consumed = 0;
                    status = on_read_cb(bucket_data, bucket_length,
                                        &consumed, pdone, puser);
                    if (status == APR_SUCCESS && bucket_length > consumed) {
                        /* We have data left in the bucket. Split it. */
                        status = apr_bucket_split(bucket, consumed);
                    }
                    readlen += consumed;
                }
            }
        }
        apr_bucket_delete(bucket);
    }
    if (readlen == 0 && status == APR_SUCCESS && block == APR_NONBLOCK_READ) {
        return APR_EAGAIN;
    }
    return status;
}

apr_status_t h2_conn_io_read(h2_conn_io_ctx *io,
                         apr_read_type_e block,
                         h2_conn_io_on_read_cb on_read_cb,
                         void *puser)
{
    apr_status_t status;
    int done = 0;
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, io->connection,
                  "h2_conn_io: try read, block=%d", block);
    
    if (!APR_BRIGADE_EMPTY(io->input)) {
        /* Seems something is left from a previous read, lets
         * satisfy our caller with the data we already have. */
        status = h2_conn_io_bucket_read(io, block, on_read_cb, puser, &done);
        if (status != APR_SUCCESS || done) {
            return status;
        }
        apr_brigade_cleanup(io->input);
    }
    
    status = ap_get_brigade(io->connection->input_filters,
                        io->input, AP_MODE_READBYTES,
                        block, BLOCKSIZE);
    switch (status) {
        case APR_SUCCESS:
            return h2_conn_io_bucket_read(io, block, on_read_cb, puser, &done);
        case APR_EOF:
        case APR_EAGAIN:
            break;
        default:
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, io->connection,
                          "h2_conn_io: error reading");
            break;
    }
    return status;
}

apr_status_t h2_conn_io_write(h2_conn_io_ctx *io, const char *buf, 
                              size_t length, size_t *written)
{
    *written = 0;
    
    /* we do not want to send something leftover in the brigade */
    assert(APR_BRIGADE_EMPTY(io->output));
    
    /* Append our data and pass on. */
    APR_BRIGADE_INSERT_TAIL(io->output,
            apr_bucket_transient_create((const char *)buf, length,
                                        io->output->bucket_alloc));
    
    /* Send it out through installed filters to the client */
    apr_status_t status = ap_pass_brigade(io->connection->output_filters,
                                          io->output);
    if (APLOGctrace2(io->connection)) {
        char buffer[32];
        h2_util_hex_dump(buffer, sizeof(buffer)/sizeof(buffer[0]), buf, length);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, io->connection,
                      "h2_conn_io(%ld): written %ld bytes: %s",
                      io->connection->id, length, buffer);
    }
    apr_brigade_cleanup(io->output);
    
    if (status == APR_SUCCESS
        || APR_STATUS_IS_ECONNABORTED(status)
        || APR_STATUS_IS_EPIPE(status)) {
        /* These are all fine and no reason for concern. Everything else
         * is interesting. */
        *written = length;
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, io->connection,
                      "h2_conn_io: write error");
    }

    return status;
}

apr_status_t h2_conn_io_flush(h2_conn_io_ctx *io)
{
    /* Append flush.
     */
    APR_BRIGADE_INSERT_TAIL(io->output,
                            apr_bucket_flush_create(io->output->bucket_alloc));
    
    /* Send it out through installed filters (TLS) to the client */
    apr_status_t status = ap_pass_brigade(io->connection->output_filters,
                                          io->output);
    apr_brigade_cleanup(io->output);
    
    if (status == APR_SUCCESS
        || APR_STATUS_IS_ECONNABORTED(status)
        || APR_STATUS_IS_EPIPE(status)) {
        /* These are all fine and no reason for concern. Everything else
         * is interesting. */
    }
    else {
        ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, io->connection,
                      "h2_conn_io: flush error");
    }
    
    return status;
}
