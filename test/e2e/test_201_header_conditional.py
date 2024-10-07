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
    HttpdConf().add_vhost_test1().install()
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
        r = TestEnv.curl_get(url)
        assert 200 == r["response"]["status"]
        lm = r["response"]["header"]["last-modified"]
        assert lm
        assert "vary" in r["response"]["header"]
        
        r = TestEnv.curl_get(url, options=[ "-H", "if-modified-since: %s" % lm])
        assert 304 == r["response"]["status"]
        assert "vary" in r["response"]["header"]


