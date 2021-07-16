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

#ifndef __mod_h2__h2_request__
#define __mod_h2__h2_request__

/* h2_request is the transformer of HTTP2 streams into HTTP/1.1 internal
 * format that will be fed to various httpd input filters to finally
 * become a request_rec to be handled by soemone.
 *
 * Ideally, we would make a request_rec without serializing the headers
 * we have only to make someone else parse them back.
 */
struct h2_bucket;
struct h2_mplx;

typedef struct h2_request h2_request;

struct h2_request {
    int id;            /* http2 stream id */
    
    int eoh;           /* end of headers seen */
    int eos;           /* end of input seen */
    int started;       /* request line serialized */
    int flushed;       /* http1 data has already been flushed at least once */
    
    /* pseudo header values, see ch. 8.1.2.3 */
    const char *method;
    const char *path;
    const char *authority;
    const char *scheme;
    
    struct h2_bucket *http1; /* The request serialized in HTTP/1.1 format*/
};

void h2_request_init(h2_request *req, int id);
void h2_request_destroy(h2_request *req);

apr_status_t h2_request_flush(h2_request *req, struct h2_mplx *m);

/* Return the first bucket of the request in http1 format if not
 * already flushed to the multiplexer. This data will be removed from
 * the request and is not writte out. 
 * Will return NULL of data has been flushed already.
 * Useful to directly retrieving the input for new stream tasks. 
 */
struct h2_bucket *h2_request_get_http1_start(h2_request *req, int *peos);

apr_status_t h2_request_write_header(h2_request *req,
                                     const char *name, size_t nlen,
                                     const char *value, size_t vlen,
                                     struct h2_mplx *m, apr_pool_t *pool);


apr_status_t h2_request_write_data(h2_request *request,
                                   const char *data, size_t len,
                                   struct h2_mplx *m);

apr_status_t h2_request_end_headers(h2_request *req, struct h2_mplx *m);
apr_status_t h2_request_close(h2_request *req, struct h2_mplx *m);

apr_status_t h2_request_rwrite(h2_request *req, request_rec *r,
                               struct h2_mplx *m, apr_pool_t *pool);

#endif /* defined(__mod_h2__h2_request__) */
