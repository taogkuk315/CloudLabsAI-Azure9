#
# mod-h2 test suite
# check handling of HTTP/2 Early Hints
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
    HttpdConf(
    ).start_vhost( TestEnv.HTTPS_PORT, "hints", docRoot="htdocs/test1", withSSL=True
    ).add_line("""    Protocols h2 http/1.1"

    H2EarlyHints on
    RewriteEngine on
    RewriteRule ^/006-(.*)?\\.html$ /006.html
    <Location /006-hints.html>
        H2PushResource "/006/006.css" critical
    </Location>
    <Location /006-nohints.html>
        Header add Link "</006/006.css>;rel=preload"
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
    
    # H2EarlyHints enabled in general, check that it works for H2PushResource
    def test_401_31(self):
        url = TestEnv.mkurl("https", "hints", "/006-hints.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)
        early = r["response"]["previous"]
        assert early
        assert 103 == int(early["header"][":status"])
        assert early["header"]["link"]

    # H2EarlyHints enabled in general, but does not trigger on added response headers
    def test_401_32(self):
        url = TestEnv.mkurl("https", "hints", "/006-nohints.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)
        assert not "previous" in r["response"]


