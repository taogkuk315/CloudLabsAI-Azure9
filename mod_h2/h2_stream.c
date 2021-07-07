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
#include "h2_mplx.h"
#include "h2_request.h"
#include "h2_resp_head.h"
#include "h2_stream.h"
#include "h2_task.h"
#include "h2_ctx.h"
#include "h2_task_input.h"


static void set_state(h2_stream *stream, h2_stream_state_t state)
{
    if (stream->state != state) {
        h2_stream_state_t oldstate = stream->state;
        stream->state = state;
    }
}


h2_stream *h2_stream_create(int id, conn_rec *c, struct h2_mplx *m)
{
    h2_stream *stream = apr_pcalloc(c->pool, sizeof(h2_stream));
    if (stream != NULL) {
        stream->id = id;
        stream->state = H2_STREAM_ST_IDLE;
        stream->c = c;
        stream->m = m;
    }
    return stream;
}

apr_status_t h2_stream_destroy(h2_stream *stream)
{
    if (stream->req) {
        h2_request_destroy(stream->req);
        stream->req = NULL;
    }
    stream->m = NULL;
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

apr_status_t h2_stream_write_eoh(h2_stream *stream)
{
    return h2_request_end_headers(stream->req, stream->m);
}

apr_status_t h2_stream_rwrite(h2_stream *stream, request_rec *r)
{
    if (!stream->req) {
        stream->req = h2_request_create(stream->c->pool, stream->id);
        if (!stream->req) {
            return APR_ENOMEM;
        }
    }
    return h2_request_rwrite(stream->req, r, stream->m);
}

apr_status_t h2_stream_write_eos(h2_stream *stream)
{
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, stream->c,
                  "h2_stream(%ld-%d): closing input",
                  stream->c->id, stream->id);
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
    return h2_request_close(stream->req, stream->m);
}

apr_status_t h2_stream_write_header(h2_stream *stream,
                                    const char *name, size_t nlen,
                                    const char *value, size_t vlen)
{
    if (!stream->req) {
        stream->req = h2_request_create(stream->c->pool, stream->id);
        if (!stream->req) {
            return APR_ENOMEM;
        }
    }
    return h2_request_write_header(stream->req, name, nlen,
                                   value, vlen, stream->m);
}

apr_status_t h2_stream_write_data(h2_stream *stream,
                                  const char *data, size_t len)
{
    return h2_request_write_data(stream->req, data, len, stream->m);
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

