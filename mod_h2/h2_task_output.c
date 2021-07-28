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
#include <http_connection.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_mplx.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_response.h"
#include "h2_resp_head.h"
#include "h2_task_output.h"

struct h2_task_output {
    struct h2_mplx *m;
    int session_id;
    int stream_id;
    int eos;
    struct h2_bucket *cur;
    apr_size_t cur_offset;
    
    h2_resp_head *head;
    
    h2_output_converter conv;
    void *conv_ctx;
};


static apr_status_t copy_unchanged(h2_bucket *bucket,
                                   void *conv_data,
                                   const char *data, apr_size_t len,
                                   apr_size_t *pconsumed)
{
    *pconsumed = h2_bucket_append(bucket, data, len);
    return APR_SUCCESS;
}

static apr_status_t flush_cur(h2_task_output *output)
{
    if (output->cur) {
        h2_mplx_out_write(output->m, APR_BLOCK_READ,
                          output->stream_id, output->cur);
        output->cur = NULL;
    }
    
    return APR_SUCCESS;
}

h2_task_output *h2_task_output_create(apr_pool_t *pool,
                                      int session_id, int stream_id,
                                      struct h2_mplx *m)
{
    h2_task_output *output = apr_pcalloc(pool, sizeof(h2_task_output));
    if (output) {
        output->m = m;
        h2_mplx_reference(m);
        output->stream_id = stream_id;
        output->session_id = session_id;
        output->conv = copy_unchanged;
    }
    return output;
}

void h2_task_output_destroy(h2_task_output *output)
{
    if (output->head) {
        h2_resp_head_destroy(output->head);
        output->head = NULL;
    }
    if (!output->eos) {
        h2_task_output_close(output);
    }
    if (output->cur) {
        h2_bucket_destroy(output->cur);
        output->cur = NULL;
    }
    if (output->m) {
        h2_mplx_release(output->m);
        output->m = NULL;
    }
}

void h2_task_output_close(h2_task_output *output)
{
    if (!output->eos) {
        flush_cur(output);
        h2_mplx_out_close(output->m, output->stream_id);
    }
}

void h2_task_output_set_converter(h2_task_output *output,
                                    h2_output_converter conv,
                                    void *conv_ctx)
{
    if (conv) {
        output->conv = conv;
        output->conv_ctx = conv_ctx;
    }
    else {
        output->conv = copy_unchanged;
        output->conv_ctx = NULL;
    }
}

static apr_status_t prepare_cur(h2_task_output *output)
{
    if (!output->cur) {
        output->cur = h2_bucket_alloc(BLOCKSIZE);
        if (!output->cur) {
            return APR_ENOMEM;
        }
    }
    return APR_SUCCESS;
}


static apr_status_t process_data(h2_task_output *output,
                                 ap_filter_t *filter,
                                 const char *data, apr_size_t len)
{
    apr_status_t status = APR_SUCCESS;
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, filter->c,
                  "h2_task_output(%d-%d): writing %d bytes",
                  output->session_id, output->stream_id, (int)len);
    while (len > 0) {
        status = prepare_cur(output);
        if (status != APR_SUCCESS) {
            return status;
        }
        apr_size_t consumed = 0;
        status = output->conv(output->cur, output->conv_ctx,
                              data, len, &consumed);
        if (status != APR_SUCCESS) {
            return status;
        }
        len -= consumed;
        data += consumed;
        if (h2_bucket_available(output->cur) <= 0) {
            flush_cur(output);
        }
    }
    return APR_SUCCESS;
}

apr_status_t h2_task_output_open(h2_task_output *output, h2_resp_head *response)
{
    long content_length = h2_resp_head_get_content_length(response);
    if (content_length > 0 && content_length < BLOCKSIZE) {
        /* For small responses, we wait for the remaining data to
         * come in before we announce readyness of our output. That
         * way we have less thread sync to do.
         */
        output->head = response;
        return APR_SUCCESS;
    }
    return h2_mplx_out_open(output->m, output->stream_id, response);
}

apr_status_t h2_task_output_write(h2_task_output *output,
                                    ap_filter_t* filter,
                                    apr_bucket_brigade* brigade)
{
    apr_status_t status = APR_SUCCESS;
    
    if (filter->next != NULL) {
        ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, filter->c,
                      "h2_task_output(%d-%d): unexpected filter",
                      output->session_id, output->stream_id);
    }
    
    if (APR_BRIGADE_EMPTY(brigade)) {
        return APR_SUCCESS;
    }
    
    int got_eos = 0;
    while (status == APR_SUCCESS && !APR_BRIGADE_EMPTY(brigade)) {
        apr_bucket* bucket = APR_BRIGADE_FIRST(brigade);
        
        if (APR_BUCKET_IS_METADATA(bucket)) {
            if (APR_BUCKET_IS_EOS(bucket)) {
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, filter->c,
                              "h2_task_output(%d-%d): got eos from brigade",
                              output->session_id, output->stream_id);
                output->eos = 1;
                if (output->head) {
                    h2_mplx_out_open(output->m,
                                     output->stream_id, output->head);
                    output->head = NULL;
                }
                flush_cur(output);
                h2_mplx_out_close(output->m, output->stream_id);
                got_eos = 1;
            }
            else if (APR_BUCKET_IS_FLUSH(bucket)) {
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, filter->c,
                              "h2_task_output(%d-%d): got flush from brigade",
                              output->session_id, output->stream_id);
                flush_cur(output);
            }
            else {
                /* ignore */
            }
        }
        else if (got_eos) {
            /* ignore, may happend according to apache documentation */
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, filter->c,
                          "h2_task_output(%d-%d): has data after eos",
                          output->session_id, output->stream_id);
        }
        else {
            const char* data = NULL;
            apr_size_t data_length = 0;
            status = apr_bucket_read(bucket, &data, &data_length,
                                     APR_NONBLOCK_READ);
            
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                          "h2_task_output(%d-%d): got %d bytes from brigade",
                          output->session_id, output->stream_id,
                          (int)data_length);
            
            if (status == APR_SUCCESS) {
                status = process_data(output, filter, data, data_length);
            }
            else if (status == APR_EAGAIN) {
                status = apr_bucket_read(bucket, &data, &data_length,
                                         APR_BLOCK_READ);
                if (status != APR_SUCCESS) {
                    ap_log_cerror(APLOG_MARK, APLOG_WARNING, status, filter->c,
                                  "h2_task_output(%d-%d): read failed",
                                  output->session_id, output->stream_id);
                }
                else {
                    status = process_data(output, filter, data, data_length);
                }
            }
        }
        
        apr_bucket_delete(bucket);
    }
    
    return status;
}


