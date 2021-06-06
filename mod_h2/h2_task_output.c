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
#include "h2_bucket_queue.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_task_output.h"


static apr_status_t copy_unchanged(h2_bucket *bucket,
                                   void *conv_data,
                                   const char *data, apr_size_t len,
                                   apr_size_t *pconsumed)
{
    *pconsumed = h2_bucket_append(bucket, data, len);
    return APR_SUCCESS;
}

h2_task_output *h2_task_output_create(apr_pool_t *pool,
                                          int stream_id,
                                          h2_bucket_queue *q)
{
    h2_task_output *output = apr_pcalloc(pool, sizeof(h2_task_output));
    if (output) {
        output->queue = q;
        output->stream_id = stream_id;
        output->conv = copy_unchanged;
    }
    return output;
}

void h2_task_output_destroy(h2_task_output *output)
{
    if (output->cur) {
        h2_bucket_destroy(output->cur);
        output->cur = NULL;
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


static apr_status_t flush_cur(h2_task_output *output,
                              ap_filter_t* filter)
{
    if (output->cur) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, filter->c,
                      "h2_stream(%d): flush %d bytes",
                      output->stream_id, (int)output->cur->data_len);
        h2_bucket_queue_append(output->queue, output->cur, output->stream_id);
        output->cur = NULL;
    }
    
    return APR_SUCCESS;
}

static apr_status_t process_data(h2_task_output *output,
                                 ap_filter_t *filter,
                                 const char *data, apr_size_t len)
{
    apr_status_t status = APR_SUCCESS;
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, filter->c,
                  "h2_stream(%d): output writing %d bytes",
                  output->stream_id, (int)len);
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
            flush_cur(output, filter);
        }
    }
    return APR_SUCCESS;
}

apr_status_t h2_task_output_write(h2_task_output *output,
                                    ap_filter_t* filter,
                                    apr_bucket_brigade* brigade)
{
    apr_status_t status = APR_SUCCESS;
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, filter->c,
                  "h2_stream(%d): output write", output->stream_id);
    
    if (filter->next != NULL) {
        ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, filter->c,
                      "h2_stream(%d): output has unexpected filter",
                      output->stream_id);
    }
    
    if (APR_BRIGADE_EMPTY(brigade)) {
        return APR_SUCCESS;
    }
    
    while (!APR_BRIGADE_EMPTY(brigade)) {
        apr_bucket* bucket = APR_BRIGADE_FIRST(brigade);
        int got_eos = 0;
        
        if (APR_BUCKET_IS_METADATA(bucket)) {
            if (APR_BUCKET_IS_EOS(bucket)) {
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, filter->c,
                              "h2_stream(%d): output, got eos from brigade",
                              output->stream_id);
                output->eos = 1;
                flush_cur(output, filter);
                h2_bucket_queue_append_eos(output->queue, output->stream_id);
                got_eos = 1;
            }
            else if (APR_BUCKET_IS_FLUSH(bucket)) {
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, status, filter->c,
                              "h2_stream(%d): output, got flush from brigade",
                              output->stream_id);
                flush_cur(output, filter);
            }
            else {
                /* ignore */
            }
        }
        else if (got_eos) {
            ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, filter->c,
                          "h2_stream(%d): output has data after eos",
                          output->stream_id);
        }
        else {
            // Data
            const char* data = NULL;
            apr_size_t data_length = 0;
            
            status = apr_bucket_read(bucket, &data, &data_length,
                                     APR_NONBLOCK_READ);
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                          "h2_stream(%d): output, got %d bytes from brigade",
                          output->stream_id, (int)data_length);
            if (status == APR_SUCCESS) {
                status = process_data(output, filter, data, data_length);
            }
            else if (APR_STATUS_IS_EAGAIN(status)) {
                status = apr_bucket_read(bucket, &data, &data_length,
                                         APR_BLOCK_READ);
                if (status != APR_SUCCESS) {
                    ap_log_cerror(APLOG_MARK, APLOG_WARNING, status, filter->c,
                                  "h2_stream(%d): output read failed",
                                  output->stream_id);
                    return status;
                }
                status = process_data(output, filter, data, data_length);
            }
            else {
                return status;
            }
        }
        
        apr_bucket_delete(bucket);
    }
    
    return APR_SUCCESS;
}


