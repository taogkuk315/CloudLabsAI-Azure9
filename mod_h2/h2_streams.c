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

#include "h2_session.h"
#include "h2_stream.h"
#include "h2_streams.h"


apr_status_t h2_streams_init(h2_streams *streams, int max_streams,
                             h2_session *session)
{
    streams->session = session;
    streams->max = max_streams;
    streams->streams = apr_pcalloc(session->c->pool,
                                   sizeof(h2_stream *) * max_streams);
    return APR_SUCCESS;
}

apr_status_t h2_streams_destroy(h2_streams *streams)
{
    for (int i = 0; i < streams->max; ++i) {
        h2_stream *stream = streams->streams[i];
        if (stream) {
            streams->streams[i] = NULL;
            h2_stream_destroy(stream);
        }
    }
    return APR_SUCCESS;
}

static int get_first_free(h2_streams *streams)
{
    for (int i = 0; i < streams->max; ++i) {
        if (streams->streams[i] == NULL) {
            return i;
        }
    }
    return -1;
}

static int get_stream_index(h2_streams *streams, int stream_id)
{
    for (int i = 0; i < streams->max; ++i) {
        h2_stream *stream = streams->streams[i];
        if (stream && stream_id == stream->id) {
            return i;
        }
    }
    return -1;
}

apr_status_t h2_streams_stream_create(h2_streams *streams,
                                      struct h2_stream **stream,
                                      int stream_id)
{
    int index = get_stream_index(streams, stream_id);
    if (index >= 0) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, streams->session->c,
                      "h2_streams: creating stream that already exists: %d",
                      stream_id);
        return APR_EEXIST;
    }
    index = get_first_free(streams);
    if (index < 0) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, streams->session->c,
                      "h2_streams: creating stream, no more free slots for %d",
                      stream_id);
        return APR_EGENERAL;
    }
    
    h2_stream_create(&streams->streams[index],
                     stream_id, H2_STREAM_ST_IDLE, streams->session);
    *stream = streams->streams[index];
    return APR_SUCCESS;
}

apr_status_t h2_streams_stream_destroy(h2_streams *streams, int stream_id)
{
    int index = get_stream_index(streams, stream_id);
    if (index < 0) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, streams->session->c,
                      "h2_streams: destroying stream, not found %d",
                      stream_id);
        return APR_ENOENT;
    }
    h2_stream *stream = streams->streams[index];
    streams->streams[index] = NULL;
    return h2_stream_destroy(stream);
}

h2_stream *h2_streams_get(h2_streams *streams, int stream_id)
{
    int index = get_stream_index(streams, stream_id);
    return (index >= 0)? streams->streams[index] : NULL;
}
