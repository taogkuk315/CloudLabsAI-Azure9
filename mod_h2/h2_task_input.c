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
#include "h2_task_input.h"

struct h2_task_input {
    struct h2_mplx *m;
    int session_id;
    int stream_id;
    int eos;
    struct h2_bucket *cur;
    apr_size_t cur_offset;
};


static int check_abort(h2_task_input *input,
                       ap_filter_t *filter,
                       apr_bucket_brigade *brigade)
{
    if (filter->c->aborted) {
        APR_BRIGADE_INSERT_TAIL(brigade,
                                apr_bucket_eos_create(filter->c->bucket_alloc));
        return 0;
    }
    return 1;
}

/** stream is in state of input closed. That means no input is pending
 * on the connection and all input (if any) is in the input queue.
 */
static int all_queued(h2_task_input *input)
{
    return input->eos || h2_mplx_in_has_eos_for(input->m, input->stream_id);
}

static void cleanup(h2_task_input *input) {
    if (input->cur) {
        /* we read all in a previous call, time to remove
         * this bucket. */
        h2_bucket_destroy(input->cur);
        input->cur = NULL;
        input->cur_offset = 0;
    }
}

h2_task_input *h2_task_input_create(apr_pool_t *pool,
                                        int session_id, int stream_id,
                                        h2_bucket *data, h2_mplx *m)
{
    h2_task_input *input = apr_pcalloc(pool, sizeof(h2_task_input));
    if (input) {
        input->m = m;
        input->cur = data;
        h2_mplx_reference(m);
        input->session_id = session_id;
        input->stream_id = stream_id;
    }
    return input;
}

void h2_task_input_destroy(h2_task_input *input)
{
    if (input->cur) {
        h2_bucket_destroy(input->cur);
        input->cur = NULL;
    }
    if (input->m) {
        h2_mplx_release(input->m);
        input->m = NULL;
    }
}

apr_status_t h2_task_input_read(h2_task_input *input,
                                  ap_filter_t* filter,
                                  apr_bucket_brigade* brigade,
                                  ap_input_mode_t mode,
                                  apr_read_type_e block,
                                  apr_off_t readbytes)
{
    apr_status_t status = APR_SUCCESS;
    apr_size_t nread = 0;
    int all_there = all_queued(input);
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE3, 0, filter->c,
                  "h2_task_input(%d-%d): read() asking for %d bytes",
                  input->session_id, input->stream_id, (int)readbytes);
    
    if (input->cur && input->cur_offset >= input->cur->data_len) {
        cleanup(input);
    }
    
    if (input->eos) {
        cleanup(input);
        return APR_EOF;
    }
    
    if (!input->cur) {
        /* Try to get new data for our stream from the queue.
         * If all data is in queue (could be none), do not block.
         * Getting none back in that case means we reached the
         * end of the input.
         */
        if (!check_abort(input, filter, brigade)) {
            return APR_ECONNABORTED;
        }
        
        status = h2_mplx_in_read(input->m, all_there? APR_NONBLOCK_READ : block,
                                     input->stream_id, &input->cur);
        input->cur_offset = 0;
        if (status == APR_EOF) {
            input->eos = 1;
        }
    }
    
    if (input->cur) {
        /* Got data, depends on the read mode how much we return. */
        h2_bucket *b = input->cur;
        apr_size_t avail = (input->cur_offset < b->data_len)?
        (b->data_len - input->cur_offset) : 0;
        if (avail > 0) {
            if (mode == AP_MODE_EXHAUSTIVE) {
                /* return all we have */
                nread = avail;
            }
            else if (mode == AP_MODE_READBYTES
                     || mode == AP_MODE_SPECULATIVE) {
                /* return not more than was asked for */
                nread = (avail > readbytes)? readbytes : avail;
            }
            else if (mode == AP_MODE_GETLINE) {
                /* Look for a linebreak in the first GetLineMax bytes.
                 * If we do not find one, return all we have. */
                apr_size_t scan = input->cur_offset;
                apr_size_t scan_max = ((b->data_len > 4096)?
                                       4096 : b->data_len);
                apr_off_t index = -1;
                for (/**/; scan < scan_max; ++scan) {
                    if (b->data[scan] == '\n') {
                        index = scan;
                        break;
                    }
                }
                
                nread = (index >= 0)? (index - input->cur_offset + 1) : avail;
            }
            else {
                /* Hmm, well. There is mode AP_MODE_EATCRLF, but we chose not
                 * to support it. Seems to work. */
                ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_ENOTIMPL, filter->c,
                              "h2_task_input, unsupported READ mode %d",
                              mode);
                return APR_ENOTIMPL;
            }
            
        }
    }
    
    if (!check_abort(input, filter, brigade)) {
        return APR_ECONNABORTED;
    }
    
    if (nread > 0) {
        /* We got actual data. */
        APR_BRIGADE_INSERT_TAIL(brigade,
                                apr_bucket_transient_create(input->cur->data + input->cur_offset,
                                                            nread, brigade->bucket_alloc));
        if (mode != AP_MODE_SPECULATIVE) {
            input->cur_offset += nread;
            ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, filter->c,
                          "h2_task_input(%d-%d): forward %d bytes",
                          input->session_id, input->stream_id, (int)nread);
        }
    }
    else if (all_there) {
        /* we know there is nothing more to come and inserted all data
         * there is into the brigade. Send the EOS right away, saving
         * everyone some work. */
        APR_BRIGADE_INSERT_TAIL(brigade,
                                apr_bucket_eos_create(brigade->bucket_alloc));
        input->eos = 1;
    }
    
    if (nread == 0 && !input->eos && block == APR_NONBLOCK_READ) {
        /* no EOS, no data and call was non blocking. Caller may try again. */
        return APR_EAGAIN;
    }
    
    return APR_SUCCESS;
}

