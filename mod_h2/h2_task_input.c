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
#include "h2_conn.h"
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
    int eos;
    apr_bucket_brigade *bb;
};


static int is_aborted(h2_task_input *input, ap_filter_t *f)
{
    return (f->c->aborted || h2_task_is_aborted(input->task));
}

static int ser_header(void *ctx, const char *name, const char *value) 
{
    h2_task_input *input = (h2_task_input*)ctx;
    apr_brigade_printf(input->bb, NULL, NULL, "%s: %s\r\n", name, value);
    return 1;
}

h2_task_input *h2_task_input_create(apr_pool_t *pool, h2_task *task, 
                                    int stream_id, 
                                    apr_bucket_alloc_t *bucket_alloc,
                                    const char *method, const char *path, 
                                    const char *authority, apr_table_t *headers, 
                                    int eos, h2_mplx *m)
{
    h2_task_input *input = apr_pcalloc(pool, sizeof(h2_task_input));
    if (input) {
        input->task = task;
        input->stream_id = stream_id;
        input->m = m;
        input->bb = apr_brigade_create(pool, bucket_alloc);
        input->eos = eos;
        
        apr_brigade_printf(input->bb, NULL, NULL, "%s %s HTTP/1.1\r\n", 
                           method, path);
        apr_table_do(ser_header, input, headers, NULL);
        apr_brigade_puts(input->bb, NULL, NULL, "\r\n");
        if (input->eos) {
            APR_BRIGADE_INSERT_TAIL(input->bb, apr_bucket_eos_create(bucket_alloc));
        }
        
        if (APLOGcdebug(task->conn->c)) {
            char buffer[1024];
            apr_size_t len = sizeof(buffer)-1;
            apr_brigade_flatten(input->bb, buffer, &len);
            buffer[len] = 0;
            ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, task->conn->c,
                          "h2_task_input(%s): request is: %s", 
                          task->id, buffer);
        }
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
    apr_off_t bblen = 0;
    
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, input->task->conn->c,
                  "h2_task_input(%s): read, block=%d, mode=%d, readbytes=%ld", 
                  input->task->id, block, mode, (long)readbytes);
    
    if (is_aborted(input, filter)) {
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, input->task->conn->c,
                      "h2_task_input(%s): is aborted", 
                      input->task->id);
        return APR_ECONNABORTED;
    }
    
    if (mode == AP_MODE_INIT) {
        return APR_SUCCESS;
    }
    
    status = apr_brigade_length(input->bb, 1, &bblen);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_WARNING, status, input->task->conn->c,
                      "h2_task_input(%s): brigade length fail", 
                      input->task->id);
        return status;
    }
    
    while ((bblen == 0) || (mode == AP_MODE_READBYTES && bblen < readbytes)) {
        /* Get more data for our stream from mplx.
         */
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                      "h2_task_input(%s): get more data from mplx, block=%d, "
                      "readbytes=%ld, queued=%ld",
                      h2_task_get_id(input->task), block, 
                      (long)readbytes, (long)bblen);
        
        /* Although we sometimes get called with APR_NONBLOCK_READs, 
         we seem to  fill our buffer blocking. Otherwise we get EAGAIN,
         return that to our caller and everyone throws up their hands,
         never calling us again. */
        status = h2_mplx_in_read(input->m, APR_BLOCK_READ,
                                 input->stream_id, input->bb, 
                                 h2_task_get_io_cond(input->task));
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                      "h2_task_input(%s): mplx in read returned",
                      h2_task_get_id(input->task));
        if (status != APR_SUCCESS) {
            return status;
        }
        status = apr_brigade_length(input->bb, 1, &bblen);
        if (status != APR_SUCCESS) {
            return status;
        }
        if ((bblen == 0) && (block == APR_NONBLOCK_READ)) {
            return h2_util_has_eos(input->bb, 0)? APR_EOF : APR_EAGAIN;
        }
        ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                      "h2_task_input(%s): mplx in read, %ld bytes in brigade",
                      h2_task_get_id(input->task), (long)bblen);
    }
    
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, status, filter->c,
                  "h2_task_input(%s): read, mode=%d, block=%d, "
                  "readbytes=%ld, queued=%ld",
                  h2_task_get_id(input->task), mode, block, 
                  (long)readbytes, (long)bblen);
           
    if (!APR_BRIGADE_EMPTY(input->bb)) {
        if (mode == AP_MODE_EXHAUSTIVE) {
            /* return all we have */
            return h2_util_move(bb, input->bb, readbytes, 0, 
                                NULL, "task_input_read(exhaustive)");
        }
        else if (mode == AP_MODE_READBYTES) {
            return h2_util_move(bb, input->bb, readbytes, 0, 
                                NULL, "task_input_read(readbytes)");
        }
        else if (mode == AP_MODE_SPECULATIVE) {
            /* return not more than was asked for */
            return h2_util_copy(bb, input->bb, readbytes, 0, 
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

