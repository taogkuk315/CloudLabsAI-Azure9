#
# mod-h2 test suite
# check handling of conditional headers
#

import copy
import os
import re
import sys
import time
import pytest

from datetime import datetime
from TestEnv import TestEnv
from TestHttpdConf import HttpdConf

def setup_module(module):
    print("setup_module: %s" % module.__name__)
    TestEnv.init()
    HttpdConf().add_line("KeepAlive on"
    ).add_line("MaxKeepAliveRequests 30"
    ).add_line("KeepAliveTimeout 30"
    ).add_vhost_test1().install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    # check handling of 'if-modified-since' header
    def test_201_01(self):
        url = TestEnv.mkurl("https", "test1", "/006/006.css")
        r = TestEnv.curl_get(url)
        assert 200 == r["response"]["status"]
        lm = r["response"]["header"]["last-modified"]
        assert lm
        r = TestEnv.curl_get(url, options=[ "-H", "if-modified-since: %s" % lm])
        assert 304 == r["response"]["status"]
        r = TestEnv.curl_get(url, options=[ "-H", "if-modified-since: Tue, 04 Sep 2010 11:51:59 GMT"])
        assert 200 == r["response"]["status"]

    # check handling of 'if-none-match' header
    def test_201_02(self):
        url = TestEnv.mkurl("https", "test1", "/006/006.css")
        r = TestEnv.curl_get(url)
        assert 200 == r["response"]["status"]
        etag = r["response"]["header"]["etag"]
        assert etag
        r = TestEnv.curl_get(url, options=[ "-H", "if-none-match: %s" % etag])
        assert 304 == r["response"]["status"]
        r = TestEnv.curl_get(url, options=[ "-H", "if-none-match: dummy"])
        assert 200 == r["response"]["status"]
        
    @pytest.mark.skipif(True, reason="304 misses the Vary header in trunk and 2.4.x")
    def test_201_03(self):
        url = TestEnv.mkurl("https", "test1", "/006.html")
        r = TestEnv.curl_get(url, options=[ "-H", "Accept-Encoding: gzip"])
        assert 200 == r["response"]["status"]
        for h in r["response"]["header"]:
            print "%s: %s" % (h, r["response"]["header"][h])
        lm = r["response"]["header"]["last-modified"]
        assert lm
        assert "gzip" == r["response"]["header"]["content-encoding"]
        assert "Accept-Encoding" in r["response"]["header"]["vary"]
        
        r = TestEnv.curl_get(url, options=[ "-H", "if-modified-since: %s" % lm,  
            "-H", "Accept-Encoding: gzip"])
        assert 304 == r["response"]["status"]
        for h in r["response"]["header"]:
            print "%s: %s" % (h, r["response"]["header"][h])
        assert "vary" in r["response"]["header"]

    # Check if "Keep-Alive" response header is removed in HTTP/2.
    def test_201_04(self):
        url = TestEnv.mkurl("https", "test1", "/006.html")
        r = TestEnv.curl_get(url, options=[ "--http1.1", "-H", "Connection: keep-alive" ])
        assert 200 == r["response"]["status"]
        assert "timeout=30, max=30" == r["response"]["header"]["keep-alive"]
        r = TestEnv.curl_get(url, options=[ "-H", "Connection: keep-alive" ])
        assert 200 == r["response"]["status"]
        assert not "keep-alive" in r["response"]["header"]

