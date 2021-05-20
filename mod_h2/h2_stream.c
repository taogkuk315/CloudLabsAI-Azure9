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

#include "h2_private.h"
#include "h2_frame.h"
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
                              conn_rec *master,
                              h2_bucket_queue *input)
{
    conn_rec *c = NULL;
    apr_status_t status = h2_stream_conn_create(&c, master);
    if (status == APR_SUCCESS) {
        h2_stream *stream = apr_pcalloc(c->pool, sizeof(h2_stream));
        stream->id = id;
        stream->state = state;
        stream->eoh = 0;
        stream->c = c;
        stream->input = input;
        
        *pstream = stream;
        return APR_SUCCESS;
    }
    return status;
}

apr_status_t h2_stream_destroy(h2_stream *stream)
{
    if (stream->work) {
        h2_bucket_destroy(stream->work);
        stream->work = NULL;
    }
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

static apr_status_t h2_stream_check_work(h2_stream *stream)
{
    if (!stream->work) {
        stream->work = h2_bucket_alloc(16 * 1024);
        if (!stream->work) {
            return APR_ENOMEM;
        }
    }
    return APR_SUCCESS;
}

apr_status_t h2_stream_push(h2_stream *stream)
{
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, 0, stream->c,
                  "h2_stream(%d): pushing req data %s",
                  stream->id, stream->work->data);
    
    apr_status_t status = h2_bucket_queue_push(stream->input, stream->work,
                                               stream);
    if (status == APR_SUCCESS) {
        stream->work = NULL;
    }
    return status;
}

apr_status_t h2_stream_end_headers(h2_stream *stream)
{
    apr_status_t status = h2_stream_check_work(stream);
    if (status != APR_SUCCESS) {
        return status;
    }
    stream->eoh = 1;

    if (!h2_bucket_has_free(stream->work, 2)) {
        status = h2_stream_push(stream);
    }
    
    if (status == APR_SUCCESS) {
        h2_bucket_cat(stream->work, "\r\n");
        status = h2_stream_push(stream);
    }
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, stream->c,
                  "h2_stream(%d): headers done", stream->id);
    return status;
}

apr_status_t h2_stream_close_input(h2_stream *stream)
{
    apr_status_t status = APR_SUCCESS;
    switch (stream->state) {
        case H2_STREAM_ST_CLOSED_INPUT:
        case H2_STREAM_ST_CLOSED:
            break; /* ignore, idempotent */
        case H2_STREAM_ST_CLOSED_OUTPUT:
            /* both closed now */
            stream->state = H2_STREAM_ST_CLOSED;
            break;
        default:
            /* everything else we jump to here */
            stream->state = H2_STREAM_ST_CLOSED_INPUT;
            break;
    }
    if (stream->work) {
        status = h2_stream_push(stream);
    }
    ap_log_cerror(APLOG_MARK, APLOG_DEBUG, status, stream->c,
                  "h2_stream(%d): stream input closed", stream->id);
    return status;
}

apr_status_t h2_stream_close_output(h2_stream *stream)
{
    switch (stream->state) {
        case H2_STREAM_ST_CLOSED_OUTPUT:
        case H2_STREAM_ST_CLOSED:
            break; /* ignore, idempotent */
        case H2_STREAM_ST_CLOSED_INPUT:
            /* both closed now */
            stream->state = H2_STREAM_ST_CLOSED;
            break;
        default:
            /* everything else we jump to here */
            stream->state = H2_STREAM_ST_CLOSED_OUTPUT;
            break;
    }
    return APR_SUCCESS;
}

apr_status_t h2_stream_add_header(h2_stream *stream,
                                  const char *name, size_t nlen,
                                  const char *value, size_t vlen)
{
    apr_status_t status = APR_SUCCESS;
    
    if (nlen <= 0) {
        return status;
    }
    
    if (name[0] == ':') {
        /* pseudo header, see ch. 8.1.2.3, always should come first */
        if (stream->work) {
            ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, stream->c,
                          "h2_stream(%d): pseudo header after request start",
                          stream->id);
            return APR_EGENERAL;
        }
        
        if (vlen <= 0) {
            char buffer[32];
            memset(buffer, 0, 32);
            strncpy(buffer, name, (nlen > 31)? 31 : nlen);
            ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, stream->c,
                          "h2_stream(%d): pseudo header without value %s",
                          stream->id, buffer);
            status = APR_EGENERAL;
        }
        else if (H2_HEADER_METHOD_LEN == nlen
                 && !strncmp(H2_HEADER_METHOD, name, nlen)) {
            stream->method = apr_pstrndup(stream->c->pool, value, vlen);
        }
        else if (H2_HEADER_SCHEME_LEN == nlen
                 && !strncmp(H2_HEADER_SCHEME, name, nlen)) {
            stream->scheme = apr_pstrndup(stream->c->pool, value, vlen);
        }
        else if (H2_HEADER_PATH_LEN == nlen
                 && !strncmp(H2_HEADER_PATH, name, nlen)) {
            stream->path = apr_pstrndup(stream->c->pool, value, vlen);
        }
        else if (H2_HEADER_AUTH_LEN == nlen
                 && !strncmp(H2_HEADER_AUTH, name, nlen)) {
            stream->authority = apr_pstrndup(stream->c->pool, value, vlen);
        }
        else {
            char buffer[32];
            memset(buffer, 0, 32);
            strncpy(buffer, name, (nlen > 31)? 31 : nlen);
            ap_log_cerror(APLOG_MARK, APLOG_INFO, 0, stream->c,
                          "h2_stream(%d): ignoring unknown pseudo header %s",
                          stream->id, buffer);
        }
    }
    else {
        /* non-pseudo header, append to work bucket of stream */
        if (stream->work == NULL) {
            /* the first bucket of request data we generate for this stream.
             * we should have all mandatory pseudo headers now.
             */
            if (!stream->method) {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, stream->c,
                              "h2_stream(%d): header start but :method missing",
                              stream->id);
                return APR_EGENERAL;
            }
            if (!stream->path) {
                ap_log_cerror(APLOG_MARK, APLOG_ERR, 0, stream->c,
                              "h2_stream(%d): header start but :path missing",
                              stream->id);
                return APR_EGENERAL;
            }
            
            status = h2_stream_check_work(stream);
            if (status != APR_SUCCESS) {
                return status;
            }
            status = h2_frame_req_add_start(stream->work,
                                            stream->method, stream->path);
        }
        
        if (status == APR_SUCCESS) {
            status = h2_frame_req_add_header(stream->work,
                                             name, nlen, value, vlen);
            if (status == APR_ENAMETOOLONG && stream->work->data_len > 0) {
                /* header did not fit into bucket, push bucket to input and
                 * get a new one */
                status = h2_stream_push(stream);
                if (status == APR_SUCCESS) {
                    status = h2_frame_req_add_header(stream->work,
                                                     name, nlen, value, vlen);
                    /* if this still does not work, we fail */
                }
            }
        }
    }
    
    return status;
}

apr_status_t h2_stream_add_data(h2_stream *stream,
                                const char *data, size_t len)
{
    apr_status_t status = h2_stream_check_work(stream);
    if (status != APR_SUCCESS) {
        return status;
    }
    
    while (len > 0) {
        apr_size_t written = h2_bucket_append(stream->work, data, len);
        if (written < len) {
            len -= written;
            data += written;
            apr_status_t status = h2_stream_push(stream);
            if (status != APR_SUCCESS) {
                return status;
            }
        }
        else {
            len = 0;
        }
    }
    return APR_SUCCESS;
}

