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
echo "check protocols ordering"

################################################################################
# check configured protocol ordering
################################################################################

H2_PREF_URL="$1"
H1_PREF_URL="$2"

URL_PREFIX="$H2_PREF_URL"
curl_check_alpn h2 "check h2 preference" --http2

URL_PREFIX="$H1_PREF_URL"
curl_check_alpn http/1.1 "check http/1.1 preference" --http2
