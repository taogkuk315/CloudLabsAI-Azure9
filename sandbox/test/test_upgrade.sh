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
echo "test upgrade headers: http vs. https"

################################################################################
# check announcements of upgrade: possibilities
################################################################################


URL_HTTP="$1"
URL_HTTPS="$2"

URL_PREFIX="$URL_HTTP"

# should see an announcement of h2c
#
curl_check_upgrade index.html "Upgrade: h2c" "expecting h2c"
curl_check_upgrade index.html "" "expecting none (not configured)" -H'Host: noh2.example.org'
curl_check_upgrade index.html "" "expecting none (less perferred)" -H'Host: test2.example.org'



URL_PREFIX="$URL_HTTPS"

# should see an announcement of h2
#
curl_check_upgrade index.html "Upgrade: h2" "expecting h2"
curl_check_upgrade index.html "" "expecting none (not configured)" -H'Host: noh2.example.org'
curl_check_upgrade index.html "" "expecting none (less perferred)" -H'Host: test2.example.org'
