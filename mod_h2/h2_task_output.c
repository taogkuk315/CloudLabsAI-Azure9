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
#include "h2_conn.h"
#include "h2_mplx.h"
#include "h2_session.h"
#include "h2_stream.h"
#include "h2_from_h1.h"
#include "h2_response.h"
#include "h2_task_output.h"
#include "h2_task.h"
#include "h2_util.h"


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
        output->from_h1 = h2_from_h1_create(stream_id, pool, 
                                            task->conn->bucket_alloc);
        if (!output->from_h1) {
            return NULL;
        }
    }
    return output;
}

void h2_task_output_destroy(h2_task_output *output)
{
    h2_task_output_close(output);
    if (output->from_h1) {
        h2_from_h1_destroy(output->from_h1);
        output->from_h1 = NULL;
    }
}

void h2_task_output_close(h2_task_output *output)
{
    h2_mplx_out_close(output->m, output->stream_id);
}

int h2_task_output_has_started(h2_task_output *output)
{
    return output->state >= H2_TASK_OUT_STARTED;
}

static apr_status_t out_write(h2_task_output *output, ap_filter_t *f,
                              apr_bucket_brigade *bb)
{
    if (output->state == H2_TASK_OUT_INIT) {
        output->state = H2_TASK_OUT_STARTED;
        h2_response *response = h2_from_h1_get_response(output->from_h1);
        if (!response) {
            ap_log_cerror(APLOG_MARK, APLOG_WARNING, 0, f->c,
                          "h2_task_output(%s): write without response",
                          h2_task_get_id(output->task));
            h2_task_abort(output->task);
            return APR_ECONNABORTED;
        }
        
        return h2_mplx_out_open(output->m, output->stream_id, response,
                                f, bb,
                                h2_task_get_io_cond(output->task));
    }
    return h2_mplx_out_write(output->m, output->stream_id, f, bb,
                             h2_task_get_io_cond(output->task));
}

/* Bring the data from the brigade (which represents the result of the
 * request_rec out filter chain) into the h2_mplx for further sending
 * on the master connection. 
 */
apr_status_t h2_task_output_write(h2_task_output *output,
                                    ap_filter_t* f, apr_bucket_brigade* bb)
{
    apr_status_t status = APR_SUCCESS;
    
    if (APR_BRIGADE_EMPTY(bb)) {
        return APR_SUCCESS;
    }
    
    if (h2_util_has_flush_or_eos(bb)) {
        if (output->bb && !APR_BRIGADE_EMPTY(output->bb)) {
            status = h2_util_move(output->bb, bb, 0, "task_output_write1");
            status = out_write(output, f, output->bb);
            apr_brigade_cleanup(output->bb);
        }
        else {
            status = out_write(output, f, bb);
        }
    }
    else {
        if (!output->bb) {
            output->bb = apr_brigade_create(bb->p, bb->bucket_alloc);
        }
        status = h2_util_move(output->bb, bb, 0, "task_output_write2");
    }
    return status;
}

