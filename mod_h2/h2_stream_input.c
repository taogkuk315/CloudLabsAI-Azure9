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
#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_log.h>

#include "h2_stream_input.h"

static const int GetLineMax = 4096;

static int check_abort(ap_filter_t *filter,
                       apr_bucket_brigade *brigade)
{
    h2_stream_input *input = (h2_stream_input *)filter->ctx;
    if (input->aborted || filter->c->aborted) {
        APR_BRIGADE_INSERT_TAIL(brigade,
                                apr_bucket_eos_create(filter->c->bucket_alloc));
        return 0;
    }
    return 1;
}

apr_status_t h2_stream_input_consume(h2_stream_input *input, apr_size_t len)
{
    apr_status_t status = apr_thread_mutex_lock(input->lock);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    if (input->start) {
        if (input->start < input->end) {
            /* there is data left, either a previous call was speculative
             * or the caller did not want as much data as we had stored.
             * mode == AP_MODE_GETLINE is a candidate. */
            memmove(input->buffer, input->buffer + input->start,
                    input->end - input->start);
            input->end -= input->start;
            input->start = 0;
        }
        else {
            input->start = input->end = 0;
        }
    }
    
    apr_thread_cond_signal(input->has_space);
    apr_thread_mutex_unlock(input->lock);
    return status;
}

apr_status_t h2_stream_input_wait_data(h2_stream_input *input,
                                       apr_read_type_e block)
{
    apr_status_t status = apr_thread_mutex_lock(input->lock);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    // Wait for more data to arrive
    while (!input->eos && !input->aborted
           && (input->start == input->end)
           && block != APR_NONBLOCK_READ) {
        apr_thread_cond_wait(input->has_data, input->lock);
    }
    
    apr_thread_cond_signal(input->has_space);
    apr_thread_mutex_unlock(input->lock);
    return status;
}

apr_status_t h2_stream_input_push(h2_stream_input *input,
                                  const char *data, apr_size_t len)
{
    apr_status_t status = apr_thread_mutex_lock(input->lock);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    if (input->eos) {
        status = APR_EOF;
    }
    else if (input->aborted) {
        status = APR_ECONNABORTED;
    }
    else {
        /* copy over data and signal everyone waiting for it */
        /* enough room? */
        while (len > 0) {
            if (input->end < input->length) {
                apr_size_t copylen = input->length - input->end;
                if (copylen > len) {
                    copylen = len;
                }
                memcpy(input->buffer + input->end, data, copylen);
                input->end += copylen;
                len -= copylen;
                data += copylen;
            }
            else {
                /* full, wait for someone to read from it */
                apr_thread_cond_wait(input->has_space, input->lock);
            }
        }
        
        apr_thread_cond_signal(input->has_data);
        status = APR_SUCCESS;
    }
    
    apr_thread_mutex_unlock(input->lock);
    return status;
}

apr_status_t h2_stream_input_close(h2_stream_input *input)
{
    apr_status_t status = apr_thread_mutex_lock(input->lock);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    input->eos = 1;
    apr_thread_cond_broadcast(input->has_data);
    apr_thread_mutex_unlock(input->lock);
    return status;
}

apr_status_t h2_stream_input_init(h2_stream_input *input, apr_pool_t *pool,
                                  apr_size_t bufsize)
{
    apr_status_t status = APR_SUCCESS;
    
    input->buffer = apr_palloc(pool, bufsize);
    input->length = bufsize;
    input->start = input->end = 0;
    input->aborted = input->eos = 0;
    
    status = apr_thread_mutex_create(&input->lock, APR_THREAD_MUTEX_DEFAULT,
                                     pool);
    if (status == APR_SUCCESS) {
        status = apr_thread_cond_create(&input->has_data, pool);
    }
    if (status == APR_SUCCESS) {
        status = apr_thread_cond_create(&input->has_space, pool);
    }
    return status;
}

apr_status_t h2_stream_input_destroy(h2_stream_input *input)
{
    if (input->lock) {
        apr_thread_mutex_destroy(input->lock);
    }
    if (input->has_data) {
        apr_thread_cond_destroy(input->has_data);
    }
    if (input->has_space) {
        apr_thread_cond_destroy(input->has_space);
    }
    return APR_SUCCESS;
}

apr_status_t h2_stream_input_read(ap_filter_t *filter,
                                  apr_bucket_brigade *brigade,
                                  ap_input_mode_t mode,
                                  apr_read_type_e block,
                                  apr_off_t readbytes)
{
    h2_stream_input *input = (h2_stream_input *)filter->ctx;
    apr_size_t read = 0;
    apr_size_t maxread = (readbytes > 0)?
        ((readbytes > input->length)? input->length : readbytes) : 0;
    int sth_inserted = 0;

    /* The bytes we returned on a previous invocation have been
     * processed. Remove them from our buffer and signal anyone
     * wanting to give us more data that there might be room again.
     */
    h2_stream_input_consume(input, input->start);
    
    if (mode == AP_MODE_INIT) {
        return APR_SUCCESS;
    }
    
    /* No data and end-of-stream, we certainly reported this before,
     so this gives an explicity return status. */
    if (input->eos && input->end == 0) {
        return APR_EOF;
    }
    
    if (!check_abort(filter, brigade)) {
        return APR_ECONNABORTED;
    }

    if (mode == AP_MODE_READBYTES
        || mode == AP_MODE_SPECULATIVE
        || mode == AP_MODE_EXHAUSTIVE) {
        /* Read data until we have what was asked for */
        while (input->end < maxread || mode == AP_MODE_EXHAUSTIVE) {
            if (h2_stream_input_wait_data(input, block) != APR_SUCCESS) {
                break;
            }
        }
        /* If the read was not exhaustive, we return only the amount
         * asked for. */
        read = input->end;
        if (read > maxread || mode != AP_MODE_EXHAUSTIVE) {
            read = maxread;
        }
    }
    else if (mode == AP_MODE_GETLINE) {
        /* Look for a linebreak in the first GetLineMax bytes. 
         * If we do not find one, return all we have. */
        apr_size_t scan = input->start;
        apr_off_t index = -1;
        while (scan < GetLineMax && input->end < input->length) {
            for (/**/; scan < input->end && index < 0; ++scan) {
                if (input->buffer[scan] == '\n') {
                    index = scan;
                }
            }
            
            if (index >= 0
                || h2_stream_input_wait_data(input, block) != APR_SUCCESS) {
                break;
            }
        }
        
        read = (index >= 0)? index+1 : input->end;
    }
    else {
        /* Hmm, well. There is mode AP_MODE_EATCRLF, but we chose not
         * to support it. Seems to work. */
        ap_log_cerror(APLOG_MARK, APLOG_ERR, APR_ENOTIMPL, filter->c,
                      "h2_stream_input, unsupported READ mode %d",
                      mode);
        return APR_ENOTIMPL;
    }
    
    if (!check_abort(filter, brigade)) {
        return APR_ECONNABORTED;
    }
    
    if (read > 0) {
        /* We got actual data. */
        APR_BRIGADE_INSERT_TAIL(brigade,
            apr_bucket_transient_create(input->buffer, read,
                                        brigade->bucket_alloc));
        sth_inserted = 1;
    }
    
    if (input->eos && input->end == read) {
        /* we know there is nothing more to come and inserted all data
         * there is into the brigade. Send the EOS right away, saving
         * everyone some work. */
        APR_BRIGADE_INSERT_TAIL(brigade,
                                apr_bucket_eos_create(brigade->bucket_alloc));
        sth_inserted = 1;
    }

    if (!sth_inserted && block == APR_NONBLOCK_READ) {
        /* no EOS, no data and call was non blocking. Caller may try again. */
        return APR_EAGAIN;
    }
    
    if (mode != AP_MODE_SPECULATIVE) {
        /* Mark the data we inserted into the brigade as read.
         * We leave the data buffer untouched since we inserted it
         * transient into the brigade. The next read will reset
         * the offset and reuse any leftover data. */
        input->start = read;
    }
    
    return APR_SUCCESS;
}
