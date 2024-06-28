#
# mod-h2 test suite
# check ssl renegotation
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
    HttpdConf(
    ).add_line("      SSLCipherSuite ECDHE-RSA-AES256-GCM-SHA384"
    ).add_line("      <Directory \"%s/htdocs/ssl-client-verify\">" % TestEnv.WEBROOT
    ).add_line("        Require all granted"
    ).add_line("        SSLVerifyClient require"
    ).add_line("        SSLVerifyDepth 0"
    ).add_line("      </Directory>"
    ).start_vhost( TestEnv.HTTPS_PORT, "ssl", withSSL=True
    ).add_line("      Protocols h2 http/1.1"
    ).add_line("      "
    ).add_line("      <Location /renegotiate/cipher>"
    ).add_line("          SSLCipherSuite ECDHE-RSA-CHACHA20-POLY1305"
    ).add_line("      </Location>"
    ).add_line("      <Location /renegotiate/verify>"
    ).add_line("          SSLVerifyClient require"
    ).add_line("      </Location>"
    ).end_vhost(
    ).install()
    # the dir needs to exists for the configuration to have effect
    TestEnv.mkpath("%s/htdocs/ssl-client-verify" % TestEnv.WEBROOT)
    TestEnv.mkpath("%s/htdocs/renegotiate/cipher" % TestEnv.WEBROOT)
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    # access a resource with SSL renegotiation, using HTTP/1.1
    def test_101_01(self):
        url = TestEnv.mkurl("https", "ssl", "/renegotiate/cipher")
        r = TestEnv.curl_get( url, options=[ "-v", "--http1.1" ] )
        assert 0 == r["rv"]
        assert "response" in r
        assert 403 == r["response"]["status"]
        
    # try to renegotiate the cipher, should fail with correct code
    def test_101_02(self):
        url = TestEnv.mkurl("https", "ssl", "/renegotiate/cipher")
        r = TestEnv.curl_get( url, options=[ "-vvv" ] )
        assert 0 != r["rv"]
        assert not "response" in r
        assert re.search(r'HTTP_1_1_REQUIRED \(err 13\)', r["out"]["err"])
        
    # try to renegotiate a client certificate from Location 
    # needs to fail with correct code
    def test_101_03(self):
        url = TestEnv.mkurl("https", "ssl", "/renegotiate/verify")
        r = TestEnv.curl_get( url, options=[ "-vvv" ] )
        assert 0 != r["rv"]
        assert not "response" in r
        assert re.search(r'HTTP_1_1_REQUIRED \(err 13\)', r["out"]["err"])
        
    # try to renegotiate a client certificate from Directory 
    # needs to fail with correct code
    def test_101_04(self):
        url = TestEnv.mkurl("https", "ssl", "/ssl-client-verify/index.html")
        r = TestEnv.curl_get( url, options=[ "-vvv" ] )
        assert 0 != r["rv"]
        assert not "response" in r
        assert re.search(r'HTTP_1_1_REQUIRED \(err 13\)', r["out"]["err"])
        
    # make 10 requests on the same connection, none should produce a status code
    # reported by erki@example.ee
    @pytest.mark.skipif(not TestEnv.has_h2load(), reason="no h2load command available")
    def test_101_05(self):
        url = TestEnv.mkurl("https", "ssl", "/ssl-client-verify/index.html")
        r = TestEnv.run( [ TestEnv.H2LOAD, "-n", "10", "-c", "1", "-m", "1", "-vvvv", 
            "https://%s:%s/ssl-client-verify/index.html" % (TestEnv.HTTPD_ADDR, TestEnv.HTTPS_PORT)] )
        assert 0 == r["rv"]
        r = TestEnv.h2load_status(r)
        assert 10 == r["h2load"]["requests"]["total"]
        assert 10 == r["h2load"]["requests"]["started"]
        assert 10 == r["h2load"]["requests"]["done"]
        assert 0 == r["h2load"]["requests"]["succeeded"]
        assert 0 == r["h2load"]["status"]["2xx"]
        assert 0 == r["h2load"]["status"]["3xx"]
        assert 0 == r["h2load"]["status"]["4xx"]
        assert 0 == r["h2load"]["status"]["5xx"]
