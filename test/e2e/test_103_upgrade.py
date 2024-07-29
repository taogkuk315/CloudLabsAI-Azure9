#
# mod-h2 test suite
# check variable require configurations
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
    HttpdConf().add_vhost_test1().add_vhost_test2().add_vhost_noh2(
    ).start_vhost( TestEnv.HTTPS_PORT, "test3", docRoot="htdocs/test1", withSSL=True
    ).add_line("      Protocols h2 http/1.1"
    ).add_line("      Header unset Upgrade"
    ).end_vhost(
    ).start_vhost( TestEnv.HTTP_PORT, "test1b", docRoot="htdocs/test1", withSSL=False
    ).add_line("      Protocols h2c http/1.1"
    ).add_line("      H2Upgrade off"
    ).add_line("      <Location /006.html>"
    ).add_line("        H2Upgrade on"
    ).add_line("      </Location>"
    ).end_vhost(
    ).install()
        
    # the dir needs to exists for the configuration to have effect
    TestEnv.mkpath("%s/htdocs/ssl-client-verify" % TestEnv.WEBROOT)
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    # accessing http://test1, will not try h2 and advertise h2 in the response
    def test_103_01(self):
        url = TestEnv.mkurl("http", "test1", "/index.html")
        r = TestEnv.curl_get(url)
        assert 0 == r["rv"]
        assert "response" in r
        assert "upgrade" in r["response"]["header"]
        assert "h2c" == r["response"]["header"]["upgrade"]
        
    # accessing http://noh2, will not advertise, because noh2 host does not have it enabled
    def test_103_02(self):
        url = TestEnv.mkurl("http", "noh2", "/index.html")
        r = TestEnv.curl_get(url)
        assert 0 == r["rv"]
        assert "response" in r
        assert not "upgrade" in r["response"]["header"]
        
    # accessing http://test2, will not advertise, because h2 has less preference than http/1.1
    def test_103_03(self):
        url = TestEnv.mkurl("http", "test2", "/index.html")
        r = TestEnv.curl_get(url)
        assert 0 == r["rv"]
        assert "response" in r
        assert not "upgrade" in r["response"]["header"]

    # accessing https://noh2, will not advertise, because noh2 host does not have it enabled
    def test_103_04(self):
        url = TestEnv.mkurl("https", "noh2", "/index.html")
        r = TestEnv.curl_get(url)
        assert 0 == r["rv"]
        assert "response" in r
        assert not "upgrade" in r["response"]["header"]

    # accessing https://test2, will not advertise, because h2 has less preference than http/1.1
    def test_103_05(self):
        url = TestEnv.mkurl("https", "test2", "/index.html")
        r = TestEnv.curl_get(url)
        assert 0 == r["rv"]
        assert "response" in r
        assert not "upgrade" in r["response"]["header"]
        
    # accessing https://test1, will advertise h2 in the response
    def test_103_06(self):
        url = TestEnv.mkurl("https", "test1", "/index.html")
        r = TestEnv.curl_get(url, options=[ "--http1.1" ])
        assert 0 == r["rv"]
        assert "response" in r
        assert "upgrade" in r["response"]["header"]
        assert "h2" == r["response"]["header"]["upgrade"]
        
    # accessing https://test3, will not send Upgrade since it is suppressed
    def test_103_07(self):
        url = TestEnv.mkurl("https", "test3", "/index.html")
        r = TestEnv.curl_get(url, options=[ "--http1.1" ])
        assert 0 == r["rv"]
        assert "response" in r
        assert not "upgrade" in r["response"]["header"]
        

    # upgrade to h2c for a request, where h2c is preferred
    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_103_20(self):
        url = TestEnv.mkurl("http", "test1", "/index.html")
        r = TestEnv.nghttp().get(url, options=[ "-u" ])
        assert 200 == r["response"]["status"]

    # upgrade to h2c for a request where http/1.1 is preferred, but the clients upgrade
    # wish is honored nevertheless
    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_103_21(self):
        url = TestEnv.mkurl("http", "test2", "/index.html")
        r = TestEnv.nghttp().get(url, options=[ "-u" ])
        assert 404 == r["response"]["status"]

    # ugrade to h2c on a host where h2c is not enabled will fail
    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_103_22(self):
        url = TestEnv.mkurl("http", "noh2", "/index.html")
        r = TestEnv.nghttp().get(url, options=[ "-u" ])
        assert not "response" in r

    # ugrade to h2c on a host where h2c is preferred, but Upgrade is disabled
    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_103_23(self):
        url = TestEnv.mkurl("http", "test1b", "/index.html")
        r = TestEnv.nghttp().get(url, options=[ "-u" ])
        assert not "response" in r

    # ugrade to h2c on a host where h2c is preferred, but Upgrade is disabled on the server,
    # but allowed for a specific location
    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_103_23(self):
        url = TestEnv.mkurl("http", "test1b", "/006.html")
        r = TestEnv.nghttp().get(url, options=[ "-u" ])
        assert 200 == r["response"]["status"]
