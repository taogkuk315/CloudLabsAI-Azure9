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


#ifndef __h2_config_h__
#define __h2_config_h__

#include <http_config.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct {
    const char *name;
    int h2_enabled;
    int h2_set;
} h2_svr_cfg;


extern void *h2_config_create_svr( apr_pool_t *pool, server_rec *s );
extern void *h2_config_create_dir( apr_pool_t *pool, char *dir );
extern void *h2_config_merge( apr_pool_t *pool, void *basev, void *addv );

extern apr_status_t h2_config_apply_header( h2_svr_cfg *config, request_rec *r );
	
extern const command_rec h2_cmds[];

#ifdef __cplusplus
}
#endif

#endif /* __h2_config_h__ */

