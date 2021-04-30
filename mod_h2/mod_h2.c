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


#include <apr_optional.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_want.h>

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>

#include <nghttp2/nghttp2.h>

#include "h2_config.h"


static void h2_hooks( apr_pool_t *pool );

AP_DECLARE_MODULE(h2) = {
    STANDARD20_MODULE_STUFF,
    h2_config_create_dir, /* func to create per dir config */
    h2_config_merge,      /* func to merge per dir config */
    h2_config_create_svr, /* func to create per server config */
    h2_config_merge,      /* func to merge per server config */
    h2_cmds,              /* command handlers */
    h2_hooks
};

static void (*ap_request_insert_filter_fn) (request_rec * r) = NULL;
static void (*ap_request_remove_filter_fn) (request_rec * r) = NULL;

/* The module initialization. Called once as apache hook, before any multi processing
 * (threaded or not) happens. It is typically at least called twice, see
 * http://wiki.apache.org/httpd/ModuleLife
 * Since the first run is just a "practise" run, we want to initialize for real
 * only on the second try. This defeats the purpose of the first dry run a bit, since
 * apache wants to verify that a new configuration actually will work. So if we
 * have trouble with the configuration, this will only be detected when the
 * server has already switched.
 * On the other hand, when we initialize lib nghttp2, all possible crazy things might
 * happen and this might even eat threads. So, better init on the real invocation,
 * for now at least.
 */
static int h2_post_config( apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s ) {
    void *data = NULL;
    const char *mod_h2_init_key = "mod_h2_init_counter";
    apr_pool_userdata_get(&data, mod_h2_init_key, s->process->pool);
    if ( data == NULL ) {
        ap_log_error( APLOG_MARK, APLOG_DEBUG, 0, s, "initializing post config dry run");
        apr_pool_userdata_set((const void *)1, mod_h2_init_key,
                              apr_pool_cleanup_null, s->process->pool);
        return APR_SUCCESS;
    }
    ap_log_error( APLOG_MARK, APLOG_INFO, 0, s, "initializing post config for real");
    
    nghttp2_session_callbacks *callbacks;
    int rv = nghttp2_session_callbacks_new(&callbacks);
    if (rv != 0) {
        ap_log_error( APLOG_MARK, APLOG_ERR, 0, s, "nghttp2_session_callbacks_new: %s", nghttp2_strerror(rv));
        return APR_EGENERAL;
    }

    nghttp2_session *session = NULL;
    rv = nghttp2_session_server_new(&session, callbacks, NULL);
    if (rv != 0) {
        ap_log_error( APLOG_MARK, APLOG_ERR, 0, s, "nghttp2_session_server_new: %s", nghttp2_strerror(rv));
        return APR_EGENERAL;
    }
    // OK, that was just a test
    nghttp2_session_del( session );
    
    return APR_SUCCESS;
}

static void h2_core_init( apr_pool_t *p, server_rec *s ) {
    // TODO
}

/* Runs once per created child process. Perform any process related initionalization here.
 */
static void h2_child_init( apr_pool_t *p, server_rec *s ) {
    ap_log_error( APLOG_MARK, APLOG_DEBUG, 0, s, "initializing child process");
    h2_core_init( p, s );
}

static int h2_handler( request_rec *r ) {
    if ( !r->handler || strcmp( r->handler, "h2" )) {
        return DECLINED;
    }
    
    if ( r->method_number != M_GET ) {
        return HTTP_METHOD_NOT_ALLOWED;
    }
    
    ap_log_rerror( APLOG_MARK, APLOG_DEBUG, 0, r, "answering GET /h2");
    ap_set_content_type( r, "text/html; charset=utf-8" );
    ap_rputs( "<html><head></head><body><h1>Hello from mod_h2!</h1></body></html>", r );
    return APR_SUCCESS;
}

static void *h2_core_inspect( request_rec *r ) {
    // TODO: Are we interested in this request? Returns NULL if not.
    return NULL;
}

static void h2_insert_module_filters( request_rec *r ) {
    void *ctx = h2_core_inspect( r );
    if ( ctx ) {
        ap_log_rerror( APLOG_MARK, APLOG_DEBUG, 0, r, "h2 installing filters for %s", r->uri );
        ap_add_input_filter( "h2_IN", ctx, r, r->connection );
        ap_add_output_filter( "h2_OUT", ctx, r, r->connection );
    }
}

/* Install this module into the apache2 infrstructure.
 */
static void h2_hooks( apr_pool_t *pool ) {
    ap_log_perror( APLOG_MARK, APLOG_INFO, 0, pool, "installing hooks");
    
    /* Run once after configuration is set, but before mpm children initialize
     */
    ap_hook_post_config( h2_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    /* Run once after a child process has been created
     */
    static const char *const aszPred[] = { "mod_proxy.c", NULL};
    ap_hook_child_init( h2_child_init, aszPred, NULL, APR_HOOK_MIDDLE);
    
    /* Request handler installed just our of curiosity for now. We might
     * want to use this for generating a dynamic stat page in the future.
     */
    ap_hook_handler( h2_handler, NULL, NULL, APR_HOOK_MIDDLE );
    
    ap_hook_insert_filter( h2_insert_module_filters, NULL, NULL, APR_HOOK_MIDDLE ) ;
}


