#
# mod-h2 test suite
# check handling of interim responses
#

import copy
import os
import re
import sys
import time
import pytest

from datetime import datetime
from TestEnv import TestEnv
from TestEnv import HttpdConf

def setup_module(module):
    print("setup_module: %s" % module.__name__)
    TestEnv.init()
    HttpdConf().add_vhost_test1().add_vhost_cgi().install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    # check that we normally do not see an interim response
    def test_300_01(self):
        url = TestEnv.mkurl("https", "test1", "/index.html")
        r = TestEnv.curl_post_data(url, 'XYZ')
        assert 200 == r["response"]["status"]
        assert not "previous" in r["response"]

    # check that we see an interim response when we ask for it
    def test_300_02(self):
        url = TestEnv.mkurl("https", "cgi", "/echo.py")
        r = TestEnv.curl_post_data(url, 'XYZ', options=[ "-H", "expect: 100-continue" ])
        assert 200 == r["response"]["status"]
        assert "previous" in r["response"]
        assert 100 == r["response"]["previous"]["status"] 

    # check proper answer on unexpected
    def test_300_03(self):
        url = TestEnv.mkurl("https", "cgi", "/echo.py")
        r = TestEnv.curl_post_data(url, 'XYZ', options=[ "-H", "expect: the-unexpected" ])
        assert 417 == r["response"]["status"]
        assert not "previous" in r["response"]

