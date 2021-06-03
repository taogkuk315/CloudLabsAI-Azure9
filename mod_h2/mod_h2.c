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
#include <apr_optional_hooks.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_want.h>

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>
#include <http_connection.h>
#include <http_protocol.h>
#include <http_request.h>

#include "mod_h2.h"

#include "h2_stream.h"
#include "h2_conn.h"
#include "h2_task.h"
#include "h2_session.h"
#include "h2_config.h"
#include "h2_ctx.h"
#include "h2_tls.h"



static void h2_hooks(apr_pool_t *pool);

AP_DECLARE_MODULE(h2) = {
    STANDARD20_MODULE_STUFF,
    NULL,
    NULL,
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
static int h2_post_config(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s)
{
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
    
    apr_status_t status = h2_tls_init(p, s);
    return status;
}

/* Runs once per created child process. Perform any process related initionalization here.
 */
static void h2_child_init(apr_pool_t *pool, server_rec *s)
{
    /* Set up our connection processing */
    apr_status_t status = h2_conn_child_init(pool, s);
    if (status != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, status, s,
                      "initializing connection handling");
    }
}

static void *h2_core_inspect(request_rec *r)
{
    // TODO: Are we interested in this request? Returns NULL if not.
    return NULL;
}

const char *h2_get_protocol(conn_rec *c)
{
    return h2_ctx_get_protocol(c);
}

/* Install this module into the apache2 infrastructure.
 */
static void h2_hooks(apr_pool_t *pool)
{
    ap_log_perror(APLOG_MARK, APLOG_INFO, 0, pool, "installing hooks");
    
    static const char *const mod_ssl[] = { "mod_ssl.c", NULL};
    static const char *const more_core[] = { "core.c", NULL};
    
    /* Run once after configuration is set, but before mpm children initialize.
     */
    ap_hook_post_config(h2_post_config, mod_ssl, NULL, APR_HOOK_MIDDLE);
    
    /* Run once after a child process has been created.
     */
    ap_hook_child_init(h2_child_init, NULL, NULL, APR_HOOK_MIDDLE);

    h2_task_hooks_init();
    
    /* This hook runs on new connections before mod_ssl has a say.
     * Its purpose is to prevent mod_ssl from touching our pseudo-connections
     * for streams.
     */
    ap_hook_pre_connection(h2_tls_stream_pre_conn,
                           NULL, mod_ssl, APR_HOOK_FIRST);
   
    /* This hook runs on new connection after mod_ssl, but before the core
     * httpd. Its purpose is to register, if TLS is used, the ALPN callbacks
     * that enable us to chose "h2" as next procotol if the client supports it.
     */
    ap_hook_pre_connection(h2_tls_pre_conn, mod_ssl, more_core, APR_HOOK_LAST);
    
    /* When the connection processing actually starts, we might to
     * take over, if h2* was selected by ALPN on a TLS connection.
     */
    ap_hook_process_connection(h2_tls_process_conn, NULL, NULL, APR_HOOK_FIRST);
    

    /* We offer a function to other modules that lets them retrieve
     * the h2 protocol used on a connection (if any).
     */
    APR_REGISTER_OPTIONAL_FN(h2_get_protocol);
}


