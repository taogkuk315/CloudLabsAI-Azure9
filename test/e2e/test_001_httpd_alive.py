#
# mod-h2 test suite
# check that we can start httpd in our environment
#

import copy
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
    HttpdConf().install()
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)
        assert TestEnv.apache_start() == 0

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)
        assert TestEnv.apache_stop() == 0
    
    # we expect to see the document from the generic server
    def test_001_01(self):
        r = TestEnv.curl_get(TestEnv.HTTP_URL + "/alive.json", 5)
        assert r["rv"] == 0
        assert r["response"]["json"]
        assert True == r["response"]["json"]["alive"]
        assert "generic" == r["response"]["json"]["host"] 

    # we expect to see the document from the generic server
    def test_001_02(self):
        r = TestEnv.curl_get(TestEnv.HTTPS_URL + "/alive.json", 5)
        assert r["rv"] == 0
        assert r["response"]["json"]
        assert True == r["response"]["json"]["alive"]
        assert "generic" == r["response"]["json"]["host"] 

