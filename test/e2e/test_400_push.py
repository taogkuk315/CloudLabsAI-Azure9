#
# mod-h2 test suite
# check handling of HTTP/2 PUSH feature
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
    ).start_vhost( TestEnv.HTTPS_PORT, "push", docRoot="htdocs/test1", withSSL=True
    ).add_line("""    Protocols h2 http/1.1"

    RewriteEngine on
    RewriteRule ^/006-push(.*)?\.html$ /006.html
    <Location /006-push.html>
        Header add Link "</006/006.css>;rel=preload"
        Header add Link "</006/006.js>;rel=preloadX"
    </Location>
    <Location /006-push2.html>
        Header add Link "</006/006.css>;rel=preloadX, </006/006.js>; rel=preload"
    </Location>
    <Location /006-push3.html>
        Header add Link "</006/006.css>;rel=preloa,</006/006.js>;rel=preload"
    </Location>
    <Location /006-push4.html>
        Header add Link "</006/006.css;rel=preload, </006/006.js>; preload"
    </Location>
    <Location /006-push5.html>
        Header add Link '</006/006.css>;rel="preload push"'
    </Location>
    <Location /006-push6.html>
        Header add Link '</006/006.css>;rel="push preload"'
    </Location>
    <Location /006-push7.html>
        Header add Link '</006/006.css>;rel="abc preload push"'
    </Location>
    <Location /006-push8.html>
        Header add Link '</006/006.css>;rel="preload"; nopush'
    </Location>
    """).end_vhost(
    ).install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

# The push tests depend on "nghttp"
@pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    # 
    def test_400_01(self):
        url = TestEnv.mkurl("https", "push", "/006.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        assert 13 == r["response"]["id"]

    def test_400_02(self):
        url = TestEnv.mkurl("https", "push", "/006-push.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        assert 13 == r["response"]["id"]
        assert 2 == len(r["streams"])
        assert r["streams"][2]
        assert 216 == len(r["streams"][2]["response"]["body"])


