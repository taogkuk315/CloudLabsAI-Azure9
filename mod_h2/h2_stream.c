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
#include <stddef.h>

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_log.h>

#include <nghttp2/nghttp2.h>

#include "h2_private.h"
#include "h2_bucket.h"
#include "h2_mplx.h"
#include "h2_request.h"
#include "h2_stream.h"
#include "h2_task.h"
#include "h2_ctx.h"
#include "h2_task_input.h"
#include "h2_task.h"


static void set_state(h2_stream *stream, h2_stream_state_t state)
{
    if (stream->state != state) {
        h2_stream_state_t oldstate = stream->state;
        stream->state = state;
    }
}

h2_stream *h2_stream_create(int id, conn_rec *master, struct h2_mplx *m)
{
    apr_pool_t *spool = NULL;
    apr_status_t status = apr_pool_create_ex(&spool, NULL, NULL, NULL);
    if (status != APR_SUCCESS) {
        return NULL;
    }
    
    h2_stream *stream = apr_pcalloc(spool, sizeof(h2_stream));
    if (stream != NULL) {
        stream->id = id;
        stream->state = H2_STREAM_ST_IDLE;
        stream->pool = spool;
        stream->master = master;
        stream->m = m;
        stream->request = h2_request_create(id, spool);
    }
    return stream;
}

apr_status_t h2_stream_destroy(h2_stream *stream)
{
    ap_log_cerror(APLOG_MARK, APLOG_TRACE1, 0, stream->master,
                  "h2_stream(%ld-%d): destroy",
                  h2_mplx_get_id(stream->m), stream->id);
    h2_request_destroy(stream->request);
    stream->m = NULL;
    if (stream->task) {
        h2_task_destroy(stream->task);
        stream->task = NULL;
    }
    if (stream->pool) {
        apr_pool_destroy(stream->pool);
    }
    return APR_SUCCESS;
}

int h2_stream_get_id(h2_stream *stream)
{
    return stream->id;
}

void h2_stream_abort(h2_stream *stream)
{
    stream->aborted = 1;
}

h2_task *h2_stream_create_task(h2_stream *stream)
{
    int input_eos = 0;
    h2_bucket *data = h2_request_steal_first_data(stream->request, &input_eos);
    h2_request_flush(stream->request, stream->m);
    stream->task = h2_task_create(h2_mplx_get_id(stream->m),
                                  stream->id, stream->master,
                                  stream->pool,
                                  data, input_eos, stream->m);
    return stream->task;
}

apr_status_t h2_stream_write_eoh(h2_stream *stream)
{
    return h2_request_end_headers(stream->request, stream->m);
}

apr_status_t h2_stream_rwrite(h2_stream *stream, request_rec *r)
{
    return h2_request_rwrite(stream->request, r, stream->m, stream->pool);
}

apr_status_t h2_stream_write_eos(h2_stream *stream)
{
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, stream->master,
                  "h2_stream(%ld-%d): closing input",
                  h2_mplx_get_id(stream->m), stream->id);
    apr_status_t status = APR_SUCCESS;
    switch (stream->state) {
        case H2_STREAM_ST_CLOSED_INPUT:
        case H2_STREAM_ST_CLOSED:
            break; /* ignore, idempotent */
        case H2_STREAM_ST_CLOSED_OUTPUT:
            /* both closed now */
            set_state(stream, H2_STREAM_ST_CLOSED);
            break;
        default:
            /* everything else we jump to here */
            set_state(stream, H2_STREAM_ST_CLOSED_INPUT);
            break;
    }
    return h2_request_close(stream->request, stream->m);
}

apr_status_t h2_stream_write_header(h2_stream *stream,
                                    const char *name, size_t nlen,
                                    const char *value, size_t vlen)
{
    return h2_request_write_header(stream->request, name, nlen,
                                   value, vlen, stream->m,
                                   stream->pool);
}

apr_status_t h2_stream_write_data(h2_stream *stream,
                                  const char *data, size_t len)
{
    return h2_request_write_data(stream->request, data, len, stream->m);
}

apr_status_t h2_stream_read(h2_stream *stream, struct h2_bucket **pbucket)
{
    return h2_mplx_out_read(stream->m, stream->id, pbucket);
}

void h2_stream_set_suspended(h2_stream *stream, int suspended)
{
    stream->suspended = !!suspended;
}

int h2_stream_is_suspended(h2_stream *stream)
{
    return stream->suspended;
}

