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

source test_common.sh

curl_post_data upload.py "file upload via http/1.1" --http1.1 <<EOF
012345678901234567890123456789012345678901234567890123456789
EOF

curl_post_data upload.py "file upload via http/2" --http2 <<EOF
0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
EOF

