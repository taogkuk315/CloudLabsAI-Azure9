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
    HttpdConf(
    ).start_vhost( TestEnv.HTTPS_PORT, "ssl", withSSL=True
    ).add_line("      Protocols h2 http/1.1"
    ).add_line("      SSLOptions +StdEnvVars"
    ).add_line("      "
    ).add_line("      <Location /h2only.html>"
    ).add_line("          Require expr \"%{HTTP2} == 'on'\""
    ).add_line("      </Location>"
    ).add_line("      <Location /noh2.html>"
    ).add_line("          Require expr \"%{HTTP2} == 'off'\""
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

    def test_102_01(self):
        url = TestEnv.mkurl("https", "ssl", "/h2only.html")
        r = TestEnv.curl_get( url )
        assert 0 == r["rv"]
        assert "response" in r
        assert 404 == r["response"]["status"]
        
    def test_102_02(self):
        url = TestEnv.mkurl("https", "ssl", "/noh2.html")
        r = TestEnv.curl_get( url )
        assert 0 == r["rv"]
        assert "response" in r
        assert 403 == r["response"]["status"]
        

