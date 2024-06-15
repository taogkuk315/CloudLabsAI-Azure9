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
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)
        assert TestEnv.apache_start() == 0

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)
        assert TestEnv.apache_stop() == 0
    
    # check that we see the correct documents when using the test1 server name over http:
    def test_002_01(self):
        url = TestEnv.mkurl("http", "test1", "/alive.json")
        r = TestEnv.curl_get(url, 5)
        assert r["response"]["status"] == 200
        assert r["response"]["protocol"] == "HTTP/1.1"
        assert True == r["response"]["json"]["alive"]
        assert "test1" == r["response"]["json"]["host"]

    # check that we see the correct documents when using the test1 server name over https:
    def test_002_02(self):
        url = TestEnv.mkurl("https", "test1", "/alive.json")
        r = TestEnv.curl_get(url, 5)
        assert r["response"]["status"] == 200
        assert r["response"]["protocol"] == "HTTP/1.1"
        assert True == r["response"]["json"]["alive"]
        assert "test1" == r["response"]["json"]["host"]

