#
# mod-h2 test suite
# check GET use via curl
#

import copy
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
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)
        assert TestEnv.apache_start() == 0

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)
        assert TestEnv.apache_stop() == 0
    
    # check SSL environment variables from CGI script
    def test_003_01(self):
        # add config for cgi, activate by restart
        hostname = "cgi"
        conf = HttpdConf()
        conf.start_vhost( TestEnv.HTTPS_PORT, hostname, aliasList=[], docRoot="htdocs/cgi", withSSL=True)
        conf.add_line("      Protocols h2 http/1.1")
        conf.add_line("      SSLOptions +StdEnvVars")
        conf.add_line("      AddHandler cgi-script .py")
        conf.end_vhost()
        conf.install()
        assert TestEnv.apache_restart() == 0
    
        url = TestEnv.mkurl("https", hostname, "/hello.py")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/2.0" == r["response"]["json"]["protocol"]
        assert "on" == r["response"]["json"]["https"]
        assert "TLSv1.2" == r["response"]["json"]["ssl_protocol"]
        assert "on" == r["response"]["json"]["h2"]
        assert "off" == r["response"]["json"]["h2push"]

        r = TestEnv.curl_get(url, 5, [ "--http1.1" ])
        assert 200 == r["response"]["status"]
        assert "HTTP/1.1" == r["response"]["json"]["protocol"]
        assert "on" == r["response"]["json"]["https"]
        assert "TLSv1.2" == r["response"]["json"]["ssl_protocol"]
        assert "" == r["response"]["json"]["h2"]
        assert "" == r["response"]["json"]["h2push"]

    # retrieve a html file from the server and compare it to its source
    def test_003_02(self):
        with open(TestEnv.e2e_src( "htdocs/test1/index.html"), mode='rb') as file:
            src = file.read()

        url = TestEnv.mkurl("https", "test1", "/index.html")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        assert src == r["response"]["body"]

        url = TestEnv.mkurl("https", "test1", "/index.html")
        r = TestEnv.curl_get(url, 5, [ "--http1.1" ])
        assert 200 == r["response"]["status"]
        assert "HTTP/1.1" == r["response"]["protocol"]
        assert src == r["response"]["body"]
