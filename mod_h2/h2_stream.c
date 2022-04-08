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
#include <stddef.h>

#define APR_POOL_DEBUG  7

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_log.h>

#include <nghttp2/nghttp2.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_bucket_queue.h"
#include "h2_mplx.h"
#include "h2_request.h"
#include "h2_response.h"
#include "h2_stream.h"
#include "h2_task.h"
#include "h2_ctx.h"
#include "h2_task_input.h"
#include "h2_task.h"
#include "h2_util.h"


static void set_state(h2_stream *stream, h2_stream_state_t state)
{
    assert(stream);
    if (stream->state != state) {
        h2_stream_state_t oldstate = stream->state;
        stream->state = state;
    }
}

h2_stream *h2_stream_create(int id, apr_pool_t *master, 
                            apr_bucket_alloc_t *bucket_alloc, 
                            struct h2_mplx *m)
{
    apr_pool_t *spool = NULL;
    apr_status_t status = apr_pool_create(&spool, master);
    if (status != APR_SUCCESS) {
        return NULL;
    }
    
    h2_stream *stream = apr_pcalloc(spool, sizeof(h2_stream));
    if (stream != NULL) {
        stream->id = id;
        stream->state = H2_STREAM_ST_IDLE;
        stream->pool = spool;
        stream->bucket_alloc = bucket_alloc;
        stream->m = m;
        stream->input = h2_bucket_queue_create(stream->pool);
        stream->bbout = apr_brigade_create(stream->pool, 
                                           stream->bucket_alloc);

        stream->request = h2_request_create(id, spool, stream->input);
    }
    return stream;
}

apr_status_t h2_stream_destroy(h2_stream *stream)
{
    assert(stream);
    ap_log_perror(APLOG_MARK, APLOG_TRACE1, 0, stream->pool,
                  "h2_stream(%ld-%d): destroy",
                  h2_mplx_get_id(stream->m), stream->id);
    h2_request_destroy(stream->request);
    stream->m = NULL;
    if (stream->task) {
        h2_task_destroy(stream->task);
        stream->task = NULL;
    }
    if (stream->input) {
        h2_bucket_queue_destroy(stream->input);
        stream->input = NULL;
    }
    stream->bbout = NULL;
    if (stream->pool) {
        apr_pool_destroy(stream->pool);
    }
    return APR_SUCCESS;
}

int h2_stream_get_id(h2_stream *stream)
{
    assert(stream);
    return stream->id;
}

void h2_stream_abort(h2_stream *stream)
{
    assert(stream);
    stream->aborted = 1;
}

h2_task *h2_stream_create_task(h2_stream *stream, conn_rec *master)
{
    assert(stream);
    stream->task = h2_task_create(h2_mplx_get_id(stream->m), stream->id, 
                                  master, stream->pool, stream->m);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, master,
                  "h2_stream(%ld-%d): created task for %s %s (%s)",
                  h2_mplx_get_id(stream->m), stream->id,
                  stream->request->method, stream->request->path,
                  stream->request->authority);
    return stream->task;
}

apr_status_t h2_stream_in_write_eoh(h2_stream *stream)
{
    assert(stream);
    apr_status_t status = h2_request_end_headers(stream->request);
    if (status == APR_SUCCESS) {
        status = h2_request_flush(stream->request);
    }
    return status;
}

apr_status_t h2_stream_in_rwrite(h2_stream *stream, request_rec *r)
{
    assert(stream);
    set_state(stream, H2_STREAM_ST_OPEN);
    apr_status_t status = h2_request_rwrite(stream->request, r);
    return status;
}

apr_status_t h2_stream_in_write_eos(h2_stream *stream)
{
    assert(stream);
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, h2_mplx_get_conn(stream->m),
                  "h2_stream(%ld-%d): closing input",
                  h2_mplx_get_id(stream->m), stream->id);
    apr_status_t status = APR_SUCCESS;
    switch (stream->state) {
        case H2_STREAM_ST_CLOSED_INPUT:
        case H2_STREAM_ST_CLOSED:
            return APR_SUCCESS; /* ignore, idempotent */
        case H2_STREAM_ST_CLOSED_OUTPUT:
            /* both closed now */
            set_state(stream, H2_STREAM_ST_CLOSED);
            break;
        default:
            /* everything else we jump to here */
            set_state(stream, H2_STREAM_ST_CLOSED_INPUT);
            break;
    }
    return h2_request_close(stream->request);
}

apr_status_t h2_stream_in_write_header(h2_stream *stream,
                                       const char *name, size_t nlen,
                                       const char *value, size_t vlen)
{
    assert(stream);
    switch (stream->state) {
        case H2_STREAM_ST_IDLE:
            set_state(stream, H2_STREAM_ST_OPEN);
            break;
        case H2_STREAM_ST_OPEN:
            break;
        default:
            return APR_EINVAL;
    }
    return h2_request_write_header(stream->request, name, nlen, value, vlen);
}

apr_status_t h2_stream_in_write_data(h2_stream *stream,
                                     const char *data, size_t len)
{
    assert(stream);
    assert(stream);
    switch (stream->state) {
        case H2_STREAM_ST_OPEN:
            break;
        default:
            return APR_EINVAL;
    }
    return h2_request_write_data(stream->request, data, len);
}

apr_status_t h2_stream_out_read(h2_stream *stream, char *buffer, 
                                apr_size_t *plen, int *peos)
{
    apr_status_t status = APR_SUCCESS;
    apr_size_t avail = *plen;
    apr_size_t written = 0;
    
    /* As long as we read successfully, the buffer is not filled and
     * we did not encounter the eos, continue.
     */
    *peos = 0;
    while ((status == APR_SUCCESS) && (avail > 0) && !*peos) {
        
        if (APR_BRIGADE_EMPTY(stream->bbout)) {
            /* Our brigade is empty, return
             */
            if (!written) {
                status = APR_EAGAIN;
            }
            break;
        }
        
        /* Copy data in our brigade into the buffer until it is filled or
         * we encounter an EOS.
         */
        while (!APR_BRIGADE_EMPTY(stream->bbout) 
               && (status == APR_SUCCESS)
               && (avail > 0)) {
            
            apr_bucket *b = APR_BRIGADE_FIRST(stream->bbout);
            if (APR_BUCKET_IS_METADATA(b)) {
                if (APR_BUCKET_IS_EOS(b)) {
                    *peos = 1;
                }
                else {
                    /* ignore */
                }
            }
            else {
                const char *data;
                apr_size_t data_len;
                if (APR_BUCKET_IS_FILE(b)) {
                    ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, 
                                  h2_mplx_get_conn(stream->m),
                                  "h2_stream(%ld-%d): reading FILE bucket",
                                  h2_mplx_get_id(stream->m), stream->id);
                }
                if (b->length != -1 && b->length > avail) {
                    apr_bucket_split(b, avail);
                }
                status = apr_bucket_read(b, &data, &data_len, 
                                         APR_NONBLOCK_READ);
                if (status == APR_SUCCESS && data_len > 0) {
                    if (data_len > avail) {
                        apr_bucket_split(b, avail);
                        data_len = avail;
                    }
                    memcpy(buffer, data, data_len);
                    avail -= data_len;
                    buffer += data_len;
                    written += data_len;
                }
            }
            apr_bucket_delete(b);
        }
    }
    
    *plen = written;
    return status;
}

void h2_stream_out_set_suspended(h2_stream *stream, int suspended)
{
    assert(stream);
    stream->suspended = !!suspended;
}

int h2_stream_out_is_suspended(h2_stream *stream)
{
    assert(stream);
    return stream->suspended;
}

