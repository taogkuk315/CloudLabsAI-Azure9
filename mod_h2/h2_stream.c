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

#include <stddef.h>

#include <apr_strings.h>

#include <httpd.h>
#include <http_core.h>
#include <http_connection.h>
#include <http_log.h>

#include <nghttp2/nghttp2.h>

#include "h2_stream.h"

static apr_sockaddr_t *h2_sockaddr_dup(apr_sockaddr_t *in, apr_pool_t *pool)
{
    apr_sockaddr_t *out = apr_pcalloc(pool, sizeof(apr_sockaddr_t));
    memcpy(out, in, sizeof(apr_sockaddr_t));
    out->pool = pool;

    if (in->hostname != NULL) {
        out->hostname = apr_pstrdup(pool, in->hostname);
    }
    if (in->servname != NULL) {
        out->servname = apr_pstrdup(pool, in->servname);
    }
    if (in->ipaddr_ptr != NULL) {
        // ipaddr_ptr points inside the struct, towards the bits containing
        // the actual IPv4/IPv6 address (e.g. to ->sa.sin.sin_addr or
        // ->sa.sin6.sin6_addr). We point to the same offset in 'out' as was used
        // in 'in'.
        ptrdiff_t offset = (char *)in->ipaddr_ptr - (char *)in;
        out->ipaddr_ptr = (char *)out + offset;
    }
    if (in->next != NULL) {
        out->next = h2_sockaddr_dup(in->next, pool);
    }
    
    return out;
}

static apr_status_t h2_stream_conn_create(conn_rec **pc, conn_rec *master)
{
    apr_pool_t *spool = NULL;
    apr_status_t status = apr_pool_create(&spool, master->pool);
    if (status == APR_SUCCESS) {
        /* Setup a apache connection record for this stream.
         * Most of the know how borrowed from mod_spdy::slave_connection.cc
         */
        conn_rec *c = apr_pcalloc(spool, sizeof(conn_rec));
        
        c->pool = spool;
        c->bucket_alloc = apr_bucket_alloc_create(spool);
        c->conn_config = ap_create_conn_config(spool);
        c->notes = apr_table_make(spool, 5);
        
        c->base_server = master->base_server;
        c->local_addr = h2_sockaddr_dup(master->local_addr, spool);
        c->local_ip = apr_pstrdup(spool, master->local_ip);
        c->client_addr = h2_sockaddr_dup(master->client_addr, spool);
        c->client_ip = apr_pstrdup(spool, master->client_ip);
        
        c->id = master->id; // FIXME: this will not do
        
        *pc = c;
        return APR_SUCCESS;
    }
    return status;
}

apr_status_t h2_stream_create(h2_stream **pstream, int id, int state,
                              conn_rec *master)
{
    conn_rec *c = NULL;
    apr_status_t status = h2_stream_conn_create(&c, master);
    if (status == APR_SUCCESS) {
        h2_stream *stream = apr_pcalloc(c->pool, sizeof(h2_stream));
        stream->id = id;
        stream->state = state;
        stream->c = c;
        
        *pstream = stream;
        return APR_SUCCESS;
    }
    return status;
}

apr_status_t h2_stream_destroy(h2_stream *stream)
{
    h2_stream_input_destroy(&stream->input);
    apr_pool_clear(stream->c->pool);
    return APR_EGENERAL;
}

apr_status_t h2_stream_process(h2_stream *stream)
{
    apr_status_t status;
    
    /* The juicy bit here is to guess a new connection id, as it
     * needs to be unique in this httpd instance, but there is
     * no API to allocate one.
     */
    // FIXME: this will not do

    /* Furthermore, other code might want to see the socket for
     * this connection. Allocate one without further function...
     */
    apr_socket_t *socket = NULL;
    status = apr_socket_create(&socket,
                               APR_INET, SOCK_STREAM,
                               APR_PROTO_TCP, stream->c->pool);
    if (status != APR_SUCCESS) {
        ap_log_cerror(APLOG_MARK, APLOG_ERR, status, stream->c,
                      "h2_stream_process, unable to alloc socket");
        return status;
    }
    
    ap_process_connection(stream->c, socket);
    return APR_SUCCESS;
}

apr_status_t h2_stream_close_input(h2_stream *stream)
{
    switch (stream->state) {
        case H2_STREAM_ST_OPEN:
            stream->state = H2_STREAM_ST_CLOSED_INPUT;
            break;
        case H2_STREAM_ST_CLOSED_OUTPUT:
            stream->state = H2_STREAM_ST_CLOSED;
            break;
        default:
            /* ignore */
            break;
    }
    return APR_SUCCESS;
}

apr_status_t h2_stream_close_output(h2_stream *stream)
{
    switch (stream->state) {
        case H2_STREAM_ST_OPEN:
            stream->state = H2_STREAM_ST_CLOSED_OUTPUT;
            break;
        case H2_STREAM_ST_CLOSED_INPUT:
            stream->state = H2_STREAM_ST_CLOSED;
            break;
        default:
            /* ignore */
            break;
    }
    return APR_SUCCESS;
}

apr_status_t h2_stream_push(h2_stream *stream, const char *data,
                            apr_size_t length)
{
    return h2_stream_input_push(&stream->input, data, length);
}

apr_status_t h2_stream_pull(h2_stream *stream, const char *data,
                            apr_size_t length, int *eos)
{
    // TODO
    *eos = 1;
    return APR_EOF;
}

