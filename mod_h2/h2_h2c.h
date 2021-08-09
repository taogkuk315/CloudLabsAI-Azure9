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

#ifndef __mod_h2__h2_h2c__
#define __mod_h2__h2_h2c__

/* Specific function to handle the "h2c" part of a HTTP2 connection, the
 * one where plain HTTP/1 connections get Upgraded.
 */

/* Registers apache hooks for h2c protocol
 */
void h2_h2c_register_hooks(void);


#endif /* defined(__mod_h2__h2_h2c__) */
