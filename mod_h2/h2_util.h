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

struct nghttp2_frame;

int h2_util_hex_dump(char *buffer, size_t maxlen,
                     const char *data, size_t datalen);

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

/**
 * I always wanted to write my own base64url decoder...not. See 
 * https://tools.ietf.org/html/rfc4648#section-5 for description.
 */
apr_size_t h2_util_base64url_decode(unsigned char **decoded, 
                                    const char *encoded, 
                                    apr_pool_t *pool);

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

/**
 * Moves data from one brigade into another. If maxlen > 0, it only
 * moves up to maxlen bytes into the target brigade, making bucket splits
 * if needed.
 * @param to the brigade to move the data to
 * @param from the brigade to get the data from
 * @param maxlen of bytes to move, 0 for all
 */
apr_status_t h2_util_move(apr_bucket_brigade *to, apr_bucket_brigade *from, 
                          apr_size_t maxlen, int count_virtual, 
                          const char *msg);

apr_status_t h2_util_pass(apr_bucket_brigade *to, apr_bucket_brigade *from, 
                          apr_size_t maxlen, int count_virtual, 
                          const char *msg);

/**
 * Return != 0 iff there is a FLUSH or EOS bucket in the brigade.
 * @param bb the brigade to check on
 * @return != 0 iff brigade holds FLUSH or EOS bucket (or both)
 */
int h2_util_has_flush_or_eos(apr_bucket_brigade *bb);
int h2_util_has_eos(apr_bucket_brigade *bb, apr_size_t len);

#endif /* defined(__mod_h2__h2_util__) */
