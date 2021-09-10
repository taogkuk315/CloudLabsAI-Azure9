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
#
# common test functions
#

URL_PREFIX="$1"
AUTH="${URL_PREFIX#*://}"
HOST="${AUTH%%:*}"

INSTALL_DIR="../gen/install"
BIN_DIR="${INSTALL_DIR}/bin"
DOC_ROOT="htdocs/${HOST}"
GEN="gen"
TMP="$GEN/tmp"

CURL="${BIN_DIR}/curl  -sk --resolv ${HOST#*://}:127.0.0.1"
NGHTTP="${BIN_DIR}/nghttp"


fail() {
    echo "$@"
    exit 1
}


curl_check_doc() {
    DOC="$1"; shift;
    MSG="$1"; shift;
    ARGS="$@"
    echo -n "curl $URL_PREFIX/$DOC: $MSG..."
    rm -rf $TMP
    mkdir -p $TMP
    ${CURL} "$ARGS" $URL_PREFIX/$DOC > $TMP/$DOC || fail
    diff  $DOC_ROOT/$DOC $TMP/$DOC || fail
    echo ok.
}

nghttp_check_doc() {
    DOC="$1"; shift;
    MSG="$1"; shift;
    ARGS="$@"
    echo -n "nghttp $URL_PREFIX/$DOC: $MSG..."
    rm -rf $TMP &&
    mkdir -p $TMP &&
    ${NGHTTP} -u $ARGS $URL_PREFIX/$DOC > $TMP/$DOC || fail
    diff  $DOC_ROOT/$DOC $TMP/$DOC || fail
    echo ok.
}

nghttp_check_assets() {
    DOC="$1"; shift;
    MSG="$1"; shift;
    ARGS="$@"
    echo -n "nghttp $URL_PREFIX/$DOC: $MSG..."
    rm -rf $TMP &&
    mkdir -p $TMP &&
    sort > $TMP/reference
    ${NGHTTP} -uans $ARGS $URL_PREFIX/$DOC > $TMP/out || fail
    fgrep " /" $TMP/out | while read begin end dur stat size path; do
        echo "$path $size $stat"
    done | sort > $TMP/output || fail
    diff $TMP/reference $TMP/output  || fail
    echo ok.
}

curl_check_content() {
    DOC="$1"; shift;
    MSG="$1"; shift;
    ARGS="$@"
    rm -rf $TMP
    mkdir -p $TMP
    cat > $TMP/expected
    echo -n "curl $URL_PREFIX/$DOC: $MSG..."
    ${CURL} "$ARGS" $URL_PREFIX/$DOC > $TMP/$DOC || fail
    diff  $TMP/expected $TMP/$DOC || fail
    echo ok.
}
