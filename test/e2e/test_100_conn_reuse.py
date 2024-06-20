#
# mod-h2 test suite
# check connection reuse and limitations
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
    HttpdConf().add_vhost_noh2().add_vhost_cgi().install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    # make sure the protocol selection on the different hosts work as expected
    def test_100_01(self):
        # this host defaults to h2, but we can request h1
        url = TestEnv.mkurl("https", "cgi", "/hello.py")
        assert "2" == TestEnv.curl_protocol_version( url )
        assert "1.1" == TestEnv.curl_protocol_version( url, options=[ "--http1.1" ] )
        
        # this host does not enable h2, it always falls back to h1
        url = TestEnv.mkurl("https", "noh2", "/hello.py")
        assert "1.1" == TestEnv.curl_protocol_version( url )
        assert "1.1" == TestEnv.curl_protocol_version( url, options=[ "--http2" ] )

    # access a ServerAlias, after using ServerName in SNI
    def test_100_02(self):
        url = TestEnv.mkurl("https", "cgi", "/hello.py")
        hostname = ("cgi-alias.%s" % TestEnv.HTTP_TLD)
        r = TestEnv.curl_get(url, 5, [ "-HHost:%s" % hostname ])
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        assert hostname == r["response"]["json"]["host"]

    # access another vhost, after using ServerName in SNI, that uses same SSL setup
    def test_100_03(self):
        url = TestEnv.mkurl("https", "cgi", "/")
        hostname = ("test1.%s" % TestEnv.HTTP_TLD)
        r = TestEnv.curl_get(url, 5, [ "-HHost:%s" % hostname ])
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        assert "text/html" == r["response"]["header"]["content-type"]

    # access another vhost, after using ServerName in SNI, 
    # that has different SSL certificate. This triggers a 421 (misdirected request) response.
    def test_100_04(self):
        url = TestEnv.mkurl("https", "cgi", "/hello.py")
        hostname = ("noh2.%s" % TestEnv.HTTP_TLD)
        r = TestEnv.curl_get(url, 5, [ "-HHost:%s" % hostname ])
        assert 421 == r["response"]["status"]





