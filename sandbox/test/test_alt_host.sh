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
echo "alt host access: $@"

################################################################################
# check access to other hosts on same connection
################################################################################

# The correct answer is 421 and mod_h2 will created if once the SSL parse 
# request filter is no longer strict on SNI name checking. See
# https://bz.apache.org/bugzilla/show_bug.cgi?id=58007#c9
#
MISDIR_STATUS="421 Misdirected Request"

URL1="$1"
URL2="$2"

URL_PREFIX="$URL1"

nghttp_check_content index.html "noh2 host" -H'Host: noh2.example.org' <<EOF
[ERROR] HTTP/2 protocol was not selected. (nghttp2 expects h2)
Some requests were not processed. total=1, processed=0
EOF

curl_check_content hello.py "serveralias" --http2 -H'Host: test3.example.org'  <<EOF
<html>
<body>
<h2>Hello World!</h2>
PROTOCOL=HTTP/2<br/>
SSL_PROTOCOL=${EXP_SSL_PROTOCOL}<br/>
</body>
</html>
EOF

curl_check_content index.html "noh2 host" --http2 -H'Host: noh2.example.org' <<EOF
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML 2.0//EN">
<html><head>
<title>$MISDIR_STATUS</title>
</head><body>
<h1>Misdirected Request</h1>
<p>The client needs a new connection for this
request as the requested host name does not match
the Server Name Indication (SNI) in use for this
connection.</p>
</body></html>
EOF

curl_check_content index.html "unknown host" --http2 -H'Host: unknown.example.org' <<EOF
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML 2.0//EN">
<html><head>
<title>$MISDIR_STATUS</title>
</head><body>
<h1>Misdirected Request</h1>
<p>The client needs a new connection for this
request as the requested host name does not match
the Server Name Indication (SNI) in use for this
connection.</p>
</body></html>
EOF

