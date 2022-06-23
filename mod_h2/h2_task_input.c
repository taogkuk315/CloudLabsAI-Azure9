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
#include "h2_task_input.h"
#include "h2_task.h"
#include "h2_util.h"

struct h2_task_input {
    h2_task *task;
    int stream_id;
    struct h2_mplx *m;
    
    apr_bucket_brigade *bb;
};


static int is_aborted(h2_task_input *input, ap_filter_t *f)
{
    return (f->c->aborted || h2_task_is_aborted(input->task));
}

h2_task_input *h2_task_input_create(apr_pool_t *pool, h2_task *task, 
                                    int stream_id, 
                                    apr_bucket_alloc_t *bucket_alloc,
                                    h2_mplx *m)
{
    h2_task_input *input = apr_pcalloc(pool, sizeof(h2_task_input));
    if (input) {
        input->task = task;
        input->stream_id = stream_id;
        input->m = m;
        input->bb = apr_brigade_create(pool, bucket_alloc);
    }
    return input;
}

void h2_task_input_destroy(h2_task_input *input)
{
    input->bb = NULL;
}

apr_status_t h2_task_input_read(h2_task_input *input,
                                ap_filter_t* filter,
                                apr_bucket_brigade* bb,
                                ap_input_mode_t mode,
                                apr_read_type_e block,
                                apr_off_t readbytes)
{
    apr_status_t status = APR_SUCCESS;

    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                  "h2_task_input(%s): read, mode=%d, block=%d, readbytes=%ld",
                  h2_task_get_id(input->task), mode, block, (long)readbytes);
    
    if (is_aborted(input, filter)) {
        return APR_ECONNABORTED;
    }
    
    if (APR_BRIGADE_EMPTY(input->bb)) {
        /* Try to get new data for our stream from the queue.
         * If all data is in queue (could be none), do not block.
         * Getting none back in that case means we reached the
         * end of the input.
         */
        apr_off_t nread = 0;
        status = h2_mplx_in_read(input->m, block,
                                 input->stream_id, input->bb, 
                                 h2_task_get_io_cond(input->task));
        apr_brigade_length(input->bb, 1, &nread);
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                      "h2_task_input(%s): mplx in read, %ld bytes in brigade",
                      h2_task_get_id(input->task), (long)nread);
        if (status != APR_SUCCESS) {
            return status;
        }
    }
    
    if (!APR_BRIGADE_EMPTY(input->bb)) {
        if (mode == AP_MODE_EXHAUSTIVE) {
            /* return all we have */
            return h2_util_move(bb, input->bb, readbytes, 0, 
                                NULL, "task_input_read(exhaustive)");
        }
        else if (mode == AP_MODE_READBYTES) {
            return h2_util_move(bb, input->bb, readbytes, 1, 
                                NULL, "task_input_read(readbytes)");
        }
        else if (mode == AP_MODE_SPECULATIVE) {
            /* return not more than was asked for */
            return h2_util_copy(bb, input->bb, readbytes, 1, 
                                "task_input_read(speculative)");
        }
        else if (mode == AP_MODE_GETLINE) {
            /* we are reading a single LF line, e.g. the HTTP headers */
            status = apr_brigade_split_line(bb, input->bb, block, 
                                            HUGE_STRING_LEN);
            if (APLOGctrace1(filter->c)) {
                char buffer[1024];
                apr_size_t len = sizeof(buffer)-1;
                apr_brigade_flatten(bb, buffer, &len);
                buffer[len] = 0;
                ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                              "h2_task_input(%s): getline: %s",
                              h2_task_get_id(input->task), buffer);
            }
            return status;
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
    
    if (is_aborted(input, filter)) {
        return APR_ECONNABORTED;
    }
    
    return (block == APR_NONBLOCK_READ)? APR_EAGAIN : APR_EOF;
}

