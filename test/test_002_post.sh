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

CHR100="012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678
"

rm -f $GEN/data-*
i=0; while [ $i -lt 10 ]; do
    echo -n "$CHR100" >> $GEN/data-1k
    i=$[ i + 1 ]
done
i=0; while [ $i -lt 10 ]; do
    cat $GEN/data-1k >> $GEN/data-10k
    i=$[ i + 1 ]
done
i=0; while [ $i -lt 10 ]; do
    cat $GEN/data-10k >> $GEN/data-100k
    i=$[ i + 1 ]
done

i=0
rm -f $GEN/data-10k
while [ $i -lt 100 ]; do
echo -n "$CHR100" >> $GEN/data-10k
i=$[ i + 1 ]
done

# just a check that things are working
curl_post_data upload.py $GEN/data-1k "file upload via http/1.1" --http1.1

# on curl 7.40.0 and earlier, there will be a delay before the upload
# commences. Fix is underway, thanks @badger!
# Caveat: on h2c, the connection will not be upgraded, since curl sends
# the POST as first request and mod_h2 does not upgrade on requests with
# content. Currently we have no means to check that his is happening.
#
curl_post_data upload.py $GEN/data-1k "1k file upload via http/2" --http2
curl_post_data upload.py $GEN/data-10k "10k file upload via http/2" --http2
curl_post_data upload.py $GEN/data-100k "100k file upload via http/2" --http2

# Tests witht the nghttp client that *requires* h2/h2c. Sends "OPTIONS *"
# on h2c which is a good test.
#
nghttp_post_file upload.py $GEN/data-1k   "1k upload via http/2"
nghttp_post_file upload.py $GEN/data-10k  "10k upload via http/2"
nghttp_post_file upload.py $GEN/data-100k "100k upload via http/2"




