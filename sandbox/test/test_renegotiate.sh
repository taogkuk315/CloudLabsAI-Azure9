#!/bin/bash
# Copyright 2015 greenbytes GmbH (https://www.greenbytes.de)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

source $(dirname $0)/test_common.sh
echo "test renegotiate: $@"

################################################################################
# check access to other hosts on same connection
################################################################################


URL1="$1"

URL_PREFIX="$URL1"

# lookup a resource that requires TLS cipher suite renegotiation. Should
# work when using HTTP/1.1
#
curl_check_status ssl/renegotiate/cipher "404" "curl reneg cipher: http/1"

# curl does not give the RST_STREAM error anywhere, it seems. Skip this for now
#curl_check_status ssl/renegotiate/cipher "404" "curl reneg cipher: h2" --http2

# nghttp gives RST_STREAM in verbose mode, check that the given urls
# signal fallback to HTTP/1.1
#
nghttp_check_rst_error ssl/renegotiate/cipher "HTTP_1_1_REQUIRED" "nghttp reneg cipher"
nghttp_check_rst_error ssl/renegotiate/verify "HTTP_1_1_REQUIRED" "nghttp reneg verify"

