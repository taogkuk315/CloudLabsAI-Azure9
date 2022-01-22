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
#include "h2_mplx.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_from_h1.h"
#include "h2_response.h"
#include "h2_task_output.h"
#include "h2_task.h"

typedef enum {
    H2_TASK_OUT_INIT,
    H2_TASK_OUT_HEAD_DONE,
    H2_TASK_OUT_STARTED,
    H2_TASK_OUT_DONE,
} h2_task_output_state_t;

struct h2_task_output {
    h2_task *task;
    int stream_id;
    struct h2_mplx *m;
    h2_task_output_state_t state;
    struct h2_bucket *cur;
    
    h2_from_h1 *from_h1;
    h2_response *response;
};


static void converter_state_change(h2_from_h1 *resp,
                                   h2_from_h1_state_t prevstate,
                                   void *cb_ctx);

static apr_status_t flush_cur(h2_task_output *output)
{
    if (output->cur) {
        h2_mplx_out_write(output->m, APR_BLOCK_READ,
                          output->stream_id, output->cur,
                          h2_task_get_io_cond(output->task));
        output->cur = NULL;
    }
    
    return APR_SUCCESS;
}

static int is_aborted(h2_task_output *output, ap_filter_t* filter) {
    if (filter->c->aborted || h2_task_is_aborted(output->task)) {
        filter->c->aborted = 1;
        return 1;
    }
    return 0;
}

h2_task_output *h2_task_output_create(apr_pool_t *pool,
                                      h2_task *task, int stream_id,
                                      struct h2_mplx *m)
{
    h2_task_output *output = apr_pcalloc(pool, sizeof(h2_task_output));
    if (output) {
        output->task = task;
        output->stream_id = stream_id;
        output->m = m;
        output->state = H2_TASK_OUT_INIT;
        output->from_h1 = h2_from_h1_create(stream_id, pool);
        if (!output->from_h1) {
            return NULL;
        }
        h2_from_h1_set_state_change_cb(output->from_h1,
                                        converter_state_change, output);
    }
    return output;
}

void h2_task_output_destroy(h2_task_output *output)
{
    h2_task_output_close(output);
    if (output->response) {
        h2_response_destroy(output->response);
        output->response = NULL;
    }
    if (output->from_h1) {
        h2_from_h1_destroy(output->from_h1);
        output->from_h1 = NULL;
    }
    if (output->cur) {
        h2_bucket_destroy(output->cur);
        output->cur = NULL;
    }
}

void h2_task_output_close(h2_task_output *output)
{
    if (output->state != H2_TASK_OUT_DONE) {
        flush_cur(output);
        h2_mplx_out_close(output->m, output->stream_id);
        output->state = H2_TASK_OUT_DONE;
    }
}

int h2_task_output_has_started(h2_task_output *output)
{
    return output->state >= H2_TASK_OUT_STARTED;
}

apr_status_t h2_task_output_open(h2_task_output *output, h2_response *response)
{
    assert(output);
    assert(response);
    long content_length = h2_response_get_content_length(response);
    if (content_length > 0 && content_length < BLOCKSIZE) {
        /* For small responses, we wait for the remaining data to
         * come in before we announce readyness of our output. That
         * way we have less thread sync to do.
         */
        output->response = response;
        return APR_SUCCESS;
    }
    output->state = H2_TASK_OUT_STARTED;
    return h2_mplx_out_open(output->m, output->stream_id, response);
}

static apr_status_t convert_data(h2_task_output *output,
                                 ap_filter_t *filter,
                                 const char *data, apr_size_t len);

/* Bring the data from the brigade (which represents the result of the
 * request_rec out filter chain) into the h2_mplx for further sending
 * on the master connection. 
 */
apr_status_t h2_task_output_write(h2_task_output *output,
                                    ap_filter_t* filter,
                                    apr_bucket_brigade* brigade)
{
    if (filter->next != NULL) {
        ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, filter->c,
                      "h2_task_output(%s): unexpected filter",
                      h2_task_get_id(output->task));
    }
    
    if (APR_BRIGADE_EMPTY(brigade)) {
        return APR_SUCCESS;
    }
    
    int got_eos = 0;
    while (!APR_BRIGADE_EMPTY(brigade)) {
        apr_bucket* bucket = APR_BRIGADE_FIRST(brigade);
        
        if (APR_BUCKET_IS_METADATA(bucket)) {
            if (APR_BUCKET_IS_EOS(bucket)) {
                got_eos = 1;
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, filter->c,
                              "h2_task_output(%s): got eos from brigade",
                              h2_task_get_id(output->task));
                if (is_aborted(output, filter)) {
                    return APR_ECONNABORTED;
                }
                if (output->response) {
                    /* we have not placed the response into the h2_mplx yet.
                     * do so now. */
                    h2_mplx_out_open(output->m,
                                     output->stream_id, output->response);
                    output->response = NULL;
                }
                h2_task_output_close(output);
            }
            else if (APR_BUCKET_IS_FLUSH(bucket)) {
                ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, filter->c,
                              "h2_task_output(%s): got flush from brigade",
                              h2_task_get_id(output->task));
                flush_cur(output);
                if (is_aborted(output, filter)) {
                    return APR_ECONNABORTED;
                }
            }
            else {
                /* ignore */
            }
        }
        else if (got_eos) {
            /* ignore, may happen according to apache documentation */
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, filter->c,
                          "h2_task_output(%s): has data after eos",
                          h2_task_get_id(output->task));
        }
        else {
            if (APR_BUCKET_IS_FILE(bucket) 
                && h2_from_h2_is_identity(output->from_h1)) {
                /* TODO: this is the common case when static files are
                 * requested and it is worth optimizing. We would like
                 * to pass the apr_bucket_file that is inside the current
                 * bucket though the h2_mplx to our h2_session and feed
                 * it directly into the nghttp2 engine.
                 */
                ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, filter->c,
                              "h2_task_output(%s): file bucket (%ld len)",
                              h2_task_get_id(output->task), bucket->length);
            }
            /* we would like to pass this bucket directly into h2_mplx without
             * reading it. We need to be careful with allocations and life times
             * however.
             */
            const char* data = NULL;
            apr_size_t data_length = 0;
            apr_status_t status = apr_bucket_read(bucket, &data, &data_length,
                                                  APR_NONBLOCK_READ);
            
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                          "h2_task_output(%s): got %d bytes from brigade",
                          h2_task_get_id(output->task), (int)data_length);
            
            if (status == APR_SUCCESS) {
                if (is_aborted(output, filter)) {
                    return APR_ECONNABORTED;
                }
                status = convert_data(output, filter, data, data_length);
                if (status != APR_SUCCESS) {
                    return status;
                }
            }
            else if (status == APR_EAGAIN) {
                if (is_aborted(output, filter)) {
                    return APR_ECONNABORTED;
                }
                status = apr_bucket_read(bucket, &data, &data_length,
                                         APR_BLOCK_READ);
                if (status != APR_SUCCESS) {
                    ap_log_cerror(APLOG_MARK, APLOG_WARNING, status, filter->c,
                                  "h2_task_output(%s): read failed",
                                  h2_task_get_id(output->task));
                    return status;
                }
                if (is_aborted(output, filter)) {
                    return APR_ECONNABORTED;
                }
                status = convert_data(output, filter, data, data_length);
                if (status != APR_SUCCESS) {
                    return status;
                }
            }
        }
        
        ap_log_cerror(APLOG_MARK, APLOG_TRACE2, 0, filter->c,
                      "h2_task_output(%s): deleting bucket",
                      h2_task_get_id(output->task));
        apr_bucket_delete(bucket);
    }
    
    if (h2_from_h1_get_state(output->from_h1) == H2_RESP_ST_DONE) {
        h2_task_output_close(output);
    }
    return APR_SUCCESS;
}

static apr_status_t convert_data(h2_task_output *output,
                                 ap_filter_t *filter,
                                 const char *data, apr_size_t len)
{
    apr_status_t status = APR_SUCCESS;
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, filter->c,
                  "h2_task_output(%s): writing %d bytes",
                  h2_task_get_id(output->task), (int)len);
    while (len > 0) {
        if (!output->cur) {
            output->cur = h2_bucket_alloc(BLOCKSIZE);
            if (!output->cur) {
                return APR_ENOMEM;
            }
        }
        apr_size_t consumed = 0;
        status = h2_from_h1_http_convert(output->from_h1, filter->c,
                                         output->cur, data, len, &consumed);
        if (status != APR_SUCCESS) {
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, filter->c,
                          "h2_task_output(%s): write failed",
                          h2_task_get_id(output->task));
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

static void converter_state_change(h2_from_h1 *resp,
                                  h2_from_h1_state_t prevstate,
                                  void *cb_ctx)
{
    switch (h2_from_h1_get_state(resp)) {
        case H2_RESP_ST_BODY:
        case H2_RESP_ST_DONE: {
            h2_task_output *output = (h2_task_output *)cb_ctx;
            assert(output);
            if (output->state == H2_TASK_OUT_INIT) {
                output->state = H2_TASK_OUT_HEAD_DONE;
                apr_status_t status = h2_task_output_open(
                    output, h2_from_h1_get_response(output->from_h1));
                if (status != APR_SUCCESS) {
                    ap_log_perror( APLOG_MARK, APLOG_ERR, status,
                                  h2_mplx_get_pool(output->m),
                                  "task_output(%s): starting response",
                                  h2_task_get_id(output->task));
                }
            }
            break;
        }
        default:
            /* nop */
            break;
    }
}
