/* Copyright 2015 greenbytes GmbH (https://www.greenbytes.de)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __mod_h2__h2_util__
#define __mod_h2__h2_util__

int h2_util_hex_dump(char *buffer, size_t maxlen,
                     const char *data, size_t datalen);

int h2_util_frame_print(const nghttp2_frame *frame, char *buffer, size_t maxlen);

int h2_util_header_print(char *buffer, size_t maxlen,
                         const char *name, size_t namelen,
                         const char *value, size_t valuelen);

char *h2_strlwr(char *s);

/**
 * Return != 0 iff the string s contains the token, as specified in
 * HTTP header syntax, rfc7230.
 */
int h2_util_contains_token(apr_pool_t *pool, const char *s, const char *token);

const char *h2_util_first_token_match(apr_pool_t *pool, const char *s, 
                                      const char *tokens[], apr_size_t len);

#define H2_HD_MATCH_LIT(l, name, nlen)  \
    ((nlen == sizeof(l) - 1) && !apr_strnatcasecmp(l, name))

#define H2_HD_MATCH_LIT_CS(l, name)  \
    ((strlen(name) == sizeof(l) - 1) && !apr_strnatcasecmp(l, name))

#define H2_CREATE_NV_LIT_CS(nv, NAME, VALUE) nv->name = (uint8_t *)NAME;      \
                                             nv->namelen = sizeof(NAME) - 1;  \
                                             nv->value = (uint8_t *)VALUE;    \
                                             nv->valuelen = strlen(VALUE)

#define H2_CREATE_NV_CS_LIT(nv, NAME, VALUE) nv->name = (uint8_t *)NAME;      \
                                             nv->namelen = strlen(NAME);      \
                                             nv->value = (uint8_t *)VALUE;    \
                                             nv->valuelen = sizeof(VALUE) - 1

#define H2_CREATE_NV_CS_CS(nv, NAME, VALUE) nv->name = (uint8_t *)NAME;       \
                                            nv->namelen = strlen(NAME);       \
                                            nv->value = (uint8_t *)VALUE;     \
                                            nv->valuelen = strlen(VALUE)


#endif /* defined(__mod_h2__h2_util__) */
