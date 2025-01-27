#
# mod-h2 test suite
# check HTTP/2 proxied backend
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
    TestEnv.setup_data_1k_1m()
    HttpdConf().add_vhost_cgi( h2proxy_self=True  ).install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    def test_600_01(self):
        url = TestEnv.mkurl("https", "cgi", "/h2proxy/hello.py")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/2.0" == r["response"]["json"]["protocol"]
        assert "on" == r["response"]["json"]["https"]
        assert "" != r["response"]["json"]["ssl_protocol"]
        assert "on" == r["response"]["json"]["h2"]
        assert "off" == r["response"]["json"]["h2push"]


