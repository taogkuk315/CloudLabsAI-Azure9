#
# mod-h2 test suite
# check that we can use curl to make HTTP/2 requests against our server
#

import copy
import re
import sys
import time
import pytest

from datetime import datetime
from TestEnv import TestEnv

def setup_module(module):
    print("setup_module: %s" % module.__name__)
    TestEnv.init()
    assert TestEnv.apache_start() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)
    
    # check that we see the correct documents when using the test1 server name over http:
    def test_002_01(self):
        url = TestEnv.mkurl("http", "test1", "/alive.json")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/1.1" == r["response"]["protocol"]
        assert True == r["response"]["json"]["alive"]
        assert "test1" == r["response"]["json"]["host"]

    # check that we see the correct documents when using the test1 server name over https:
    def test_002_02(self):
        url = TestEnv.mkurl("https", "test1", "/alive.json")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert True == r["response"]["json"]["alive"]
        assert "test1" == r["response"]["json"]["host"]
        assert "application/json" == r["response"]["header"]["content-type"]

    # enforce HTTP/1.1
    def test_002_03(self):
        url = TestEnv.mkurl("https", "test1", "/alive.json")
        r = TestEnv.curl_get(url, 5, [ "--http1.1" ])
        assert 200 == r["response"]["status"]
        assert "HTTP/1.1" == r["response"]["protocol"]

    # enforce HTTP/2
    def test_002_04(self):
        url = TestEnv.mkurl("https", "test1", "/alive.json")
        r = TestEnv.curl_get(url, 5, [ "--http2" ])
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]

    # default is HTTP/2 on this host
    def test_002_04(self):
        url = TestEnv.mkurl("https", "test1", "/alive.json")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        assert "test1" == r["response"]["json"]["host"]

    # although, without ALPN, we cannot select it
    def test_002_05(self):
        url = TestEnv.mkurl("https", "test1", "/alive.json")
        r = TestEnv.curl_get(url, 5, [ "--no-alpn" ])
        assert 200 == r["response"]["status"]
        assert "HTTP/1.1" == r["response"]["protocol"]
        assert "test1" == r["response"]["json"]["host"]

    # default is HTTP/1.1 on the other
    def test_002_06(self):
        url = TestEnv.mkurl("https", "test2", "/alive.json")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/1.1" == r["response"]["protocol"]
        assert "test2" == r["response"]["json"]["host"]

