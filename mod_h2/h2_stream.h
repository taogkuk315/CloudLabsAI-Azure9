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


#ifndef __mod_h2__h2_stream__
#define __mod_h2__h2_stream__

/**
 * A HTTP/2 stream, e.g. a client request+response in HTTP/1.1 terms.
 *
 * A stream always belongs to a h2_session, the one managing the
 * connection to the client.
 */

struct h2_bucket;
struct h2_mplx;
struct h2_request;

typedef enum {
    H2_STREAM_ST_IDLE,
    H2_STREAM_ST_OPEN,
    H2_STREAM_ST_RESV_LOCAL,
    H2_STREAM_ST_RESV_REMOTE,
    H2_STREAM_ST_CLOSED_INPUT,
    H2_STREAM_ST_CLOSED_OUTPUT,
    H2_STREAM_ST_CLOSED,
} h2_stream_state_t;

struct h2_task;


typedef struct h2_stream {
    int id;                     /* http2 stream id */
    h2_stream_state_t state;    /* http/2 state of this stream */
    conn_rec *c;                /* the connection this stream is on */
    struct h2_mplx *m;          /* the multiplexer on this connection */
    int aborted;                /* was aborted */
    
    struct h2_request *req;     /* the request made in this stream */
    int suspended;              /* DATA sending has been suspended */
    
} h2_stream;

h2_stream *h2_stream_create(int id, conn_rec *c, struct h2_mplx *m);

apr_status_t h2_stream_destroy(h2_stream *stream);

void h2_stream_abort(h2_stream *stream);

apr_status_t h2_stream_rwrite(h2_stream *stream, request_rec *r);

apr_status_t h2_stream_write_eos(h2_stream *stream);

apr_status_t h2_stream_write_header(h2_stream *stream,
                                    const char *name, size_t nlen,
                                    const char *value, size_t vlen);

apr_status_t h2_stream_write_eoh(h2_stream *stream);

apr_status_t h2_stream_write_data(h2_stream *stream,
                                  const char *data, size_t len);

apr_status_t h2_stream_read(h2_stream *stream, struct h2_bucket **pbucket);

void h2_stream_set_suspended(h2_stream *stream, int suspended);
int h2_stream_is_suspended(h2_stream *stream);

#endif /* defined(__mod_h2__h2_stream__) */
