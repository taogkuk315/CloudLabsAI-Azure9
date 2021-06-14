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
#include "h2_private.h"

void *h2_config_create_svr(apr_pool_t *pool, server_rec *s)
{
    h2_config *conf = (h2_config *)apr_pcalloc(pool, sizeof(h2_config));

    const char *sname = s->defn_name? s->defn_name : "unknown";
    char *name = (char *)apr_pcalloc(pool, strlen(sname) + 20);
    strcpy(name, "server[");
    strcat(name, sname);
    strcat(name, "]");
    conf->name = name;

    conf->h2_enabled = 0;
    conf->h2_max_streams = 100;
    conf->h2_max_hl_size = 16 * 1024;
    conf->h2_window_size = 64 * 1024;
    conf->h2_min_workers = 32;
    conf->h2_max_workers = 256;
    return conf;
}

void *h2_config_merge(apr_pool_t *pool, void *basev, void *addv)
{
    h2_config *base = (h2_config *)basev;
    h2_config *add = (h2_config *)addv;
    h2_config *n = (h2_config *)apr_pcalloc(pool, sizeof(h2_config));

    char *name = (char *)apr_pcalloc(pool,
        20 + strlen(add->name) + strlen(base->name));
    strcpy(name, "merged[");
    strcat(name, add->name);
    strcat(name, ", ");
    strcat(name, base->name);
    strcat(name, "]");
    n->name = name;

    n->h2_enabled = (add->h2_set? add : base)->h2_enabled;
    n->h2_max_streams = (add->h2_max_streams_set? add : base)->h2_max_streams;
    n->h2_max_hl_size = (add->h2_max_hl_size_set? add : base)->h2_max_hl_size;
    n->h2_window_size = (add->h2_window_size_set? add : base)->h2_window_size;
    n->h2_min_workers = (add->h2_min_workers_set? add : base)->h2_min_workers;
    n->h2_max_workers = (add->h2_max_workers_set? add : base)->h2_max_workers;

    return n;
}

static const char *h2_conf_engine_set(cmd_parms *parms,
                                      void *arg, const char *value)
{
    h2_config *cfg = h2_config_sget(parms->server);
    cfg->h2_enabled = !strcasecmp(value, "On");
    cfg->h2_set = 1;
    
    return NULL;
}

const command_rec h2_cmds[] = {
    AP_INIT_TAKE1("H2Engine", h2_conf_engine_set, NULL,
                  RSRC_CONF, "on to enable HTTP/2 protocol handling"),
    AP_INIT_TAKE1("H2MaxSessionStreams", ap_set_int_slot,
                  (void*)APR_OFFSETOF(h2_config, h2_max_streams),
                  RSRC_CONF, "maximum number of open streams per session"),
    AP_INIT_TAKE1("H2InitialWindowSize", ap_set_int_slot,
                  (void*)APR_OFFSETOF(h2_config, h2_window_size),
                  RSRC_CONF, "initial window size on client DATA"),
    AP_INIT_TAKE1("H2MaxHeaderListSize", ap_set_int_slot,
                  (void*)APR_OFFSETOF(h2_config, h2_max_hl_size),
                  RSRC_CONF, "maximum acceptable size of request headers"),
    AP_INIT_TAKE1("H2MinWorkers", ap_set_int_slot,
                  (void*)APR_OFFSETOF(h2_config, h2_min_workers),
                  RSRC_CONF, "minimum number of worker threads per child"),
    AP_INIT_TAKE1("H2MaxWorkers", ap_set_int_slot,
                  (void*)APR_OFFSETOF(h2_config, h2_max_workers),
                  RSRC_CONF, "maximum number of worker threads per child"),
    {NULL}
};


h2_config *h2_config_sget(server_rec *s)
{
    h2_config *cfg = (h2_config *)ap_get_module_config(s->module_config, &h2_module);
    assert(cfg);
    return cfg;
}

h2_config *h2_config_get(conn_rec *c)
{
    return h2_config_sget(c->base_server);
}

