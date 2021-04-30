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
#include <http_config.h>
#include <http_log.h>

#include <apr_strings.h>

#include "h2_config.h"

APLOG_USE_MODULE(h2);

void *h2_config_create_svr( apr_pool_t *pool, server_rec *s ) {
  	//ap_log_error( APLOG_MARK, APLOG_DEBUG, 0, s, "create server config: %s", s->defn_name );
    h2_svr_cfg *conf = (h2_svr_cfg *) apr_pcalloc( pool, sizeof( h2_svr_cfg ) );

    const char *sname = s->defn_name? s->defn_name : "unknown";
    char *name = (char *)apr_pcalloc( pool, strlen(sname) + 20);
    strcpy(name, "server[");
    strcat(name, sname);
    strcat(name, "]");
    conf->name = name;

	ap_log_error( APLOG_MARK, APLOG_INFO, 0, s, "created h2 config: %s", conf->name );
    return conf;
}

void *h2_config_create_dir( apr_pool_t *pool, char *dir ) {
    h2_svr_cfg *conf = (h2_svr_cfg *) apr_pcalloc( pool, sizeof( h2_svr_cfg ) );

    const char *dname = dir? dir : "unknown";
    char *name = (char *)apr_pcalloc( pool, strlen(dname) + 20);
    strcpy(name, "dir[");
    strcat(name, dname);
    strcat(name, "]");
    conf->name = name;

	ap_log_perror( APLOG_MARK, APLOG_DEBUG, 0, pool, "created h2 config: %s", conf->name );
    return conf;
}

void *h2_config_merge( apr_pool_t *pool, void *basev, void *addv ) {
    h2_svr_cfg *base = (h2_svr_cfg *)basev;
    h2_svr_cfg *add = (h2_svr_cfg *)addv;
    h2_svr_cfg *n = (h2_svr_cfg *)apr_pcalloc( pool, sizeof(h2_svr_cfg) );

    char *name = (char *)apr_pcalloc( pool, 20 + strlen(add->name) + strlen(base->name) );
    strcpy(name, "merged[");
    strcat(name, add->name);
    strcat(name, ", ");
    strcat(name, base->name);
    strcat(name, "]");
    n->name = name;

    n->h2_enabled = add->h2_set? add->h2_enabled : base->h2_enabled;

    return n;
}

static const char *set_h2(cmd_parms *parms, void *arg, const char *value) {
    server_rec *server = parms->server;
    h2_svr_cfg *config = (h2_svr_cfg *)ap_get_module_config(server->module_config, &h2_module);
    
    config->h2_enabled = !strcasecmp(value, "on");
    config->h2_set = 1;
    
    return NULL;
}

const command_rec h2_cmds[] = {
    AP_INIT_TAKE1(
    "H2", set_h2, NULL,
        RSRC_CONF, "on to enable HTTP/2 protocol handling" ),
    {NULL}
};


