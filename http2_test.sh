#!/bin/bash
set -u

function curl() {
	command curl "$@" --limit-rate 5M \
		-o /dev/null -w "HTTP/%{http_version}" -m 5 \
		2>&1 | grep -v 'Operation timed out after'
	echo; echo
}

[ "$#" -eq 1 ] || {
	echo "Usage: $0 URL" >&2
	exit 1
}

SRV="$1"; shift

###

UNIQ_ID="test_id=$(date +%s)"
URI="$SRV?$UNIQ_ID"

echo "### Performing test with ID $UNIQ_ID"
echo "###   grep $UNIQ_ID /var/log/access.mod_h2_issue_203"
echo

echo "# http2_whole_ssl"
curl --http2   "https://$URI&_http2_whole_ssl___"

echo "# http11_whole_ssl"
curl --http1.1 "https://$URI&http11_whole_ssl___"

echo "# http10_whole_ssl"
curl --http1.0 "https://$URI&http10_whole_ssl___"

###

echo "# http2_whole_nonssl"
curl --http2   "http://$URI&_http2_whole_nonssl"

echo "# http11_whole_nonssl"
curl --http1.1 "http://$URI&http11_whole_nonssl"

echo "# http10_whole_nonssl"
curl --http1.0 "http://$URI&http10_whole_nonssl"

###

echo "# http2_half_ssl"
curl --http2   -H "Range: bytes=0-52428800" "https://$URI&_http2_half_ssl____"

echo "# http11_half_ssl"
curl --http1.1 -H "Range: bytes=0-52428800" "https://$URI&http11_half_ssl____"
