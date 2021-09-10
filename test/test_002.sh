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

################################################################################
# check cgi generated content
################################################################################
curl_check_content hello.py "default" <<EOF
<html>
<body>
<h2>Hello World!</h2>
</body>
</html>

EOF

curl_check_content hello.py "http/1.1" --http1.1 <<EOF
<html>
<body>
<h2>Hello World!</h2>
</body>
</html>

EOF

curl_check_content hello.py "http2"    --http2 <<EOF
<html>
<body>
<h2>Hello World!</h2>
</body>
</html>

EOF
