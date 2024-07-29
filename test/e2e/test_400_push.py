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
    <Location /006-push20.html>
        H2PushResource "/006/006.css" critical
        H2PushResource "/006/006.js"
    </Location>    
    <Location /006-push30.html>
        H2Push off
        Header add Link '</006/006.css>;rel="preload"'
    </Location>
    <Location /006-push31.html>
        H2PushResource "/006/006.css" critical
    </Location>
    <Location /006-push32.html>
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
    
    ############################
    # Link: header handling, various combinations

    # plain resource without configured pushes 
    def test_400_00(self):
        url = TestEnv.mkurl("https", "push", "/006.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 0 == len(promises)

    # 2 link headers configured, only 1 triggers push
    def test_400_01(self):
        url = TestEnv.mkurl("https", "push", "/006-push.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)
        assert '/006/006.css' == promises[0]["request"]["header"][":path"]
        assert 216 == len(promises[0]["response"]["body"])

    # Same as 400_01, but with single header line configured
    def test_400_02(self):
        url = TestEnv.mkurl("https", "push", "/006-push2.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)
        assert '/006/006.js' == promises[0]["request"]["header"][":path"]

    # 2 Links, only one with correct rel attribue
    def test_400_03(self):
        url = TestEnv.mkurl("https", "push", "/006-push3.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)
        assert '/006/006.js' == promises[0]["request"]["header"][":path"]

    # Missing > in Link header, PUSH not triggered
    def test_400_04(self):
        url = TestEnv.mkurl("https", "push", "/006-push4.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 0 == len(promises)

    # More than one value in "rel" parameter
    def test_400_05(self):
        url = TestEnv.mkurl("https", "push", "/006-push5.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)
        assert '/006/006.css' == promises[0]["request"]["header"][":path"]

    # Another "rel" parameter variation
    def test_400_06(self):
        url = TestEnv.mkurl("https", "push", "/006-push6.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)
        assert '/006/006.css' == promises[0]["request"]["header"][":path"]

    # Another "rel" parameter variation
    def test_400_07(self):
        url = TestEnv.mkurl("https", "push", "/006-push7.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)
        assert '/006/006.css' == promises[0]["request"]["header"][":path"]

    # Pushable link header with "nopush" attribute
    def test_400_08(self):
        url = TestEnv.mkurl("https", "push", "/006-push8.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 0 == len(promises)


    #########################
    # H2PushResource configurations

    # 2 H2PushResource config trigger on GET, but not on POST
    def test_400_20(self):
        url = TestEnv.mkurl("https", "push", "/006-push20.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 2 == len(promises)

        fpath = os.path.join(TestEnv.GEN_DIR, "data-400-20")
        with open(fpath, 'w') as f:
            f.write("test upload data")
        r = TestEnv.nghttp().upload(url, fpath)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 0 == len(promises)
    

    #########################
    # Other PUSH configurations
    
    # H2Push configured Off in location
    def test_400_30(self):
        url = TestEnv.mkurl("https", "push", "/006-push30.html")
        r = TestEnv.nghttp().get(url)
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 0 == len(promises)


    #########################
    # Push-Policy Tests <https://tools.ietf.org/html/draft-ruellan-http-accept-push-policy-00>
    
    # - suppress PUSH
    def test_400_50(self):
        url = TestEnv.mkurl("https", "push", "/006-push.html")
        r = TestEnv.nghttp().get(url, options=[ '-H', 'accept-push-policy: none'])
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 0 == len(promises)

    # - default pushes desired
    def test_400_51(self):
        url = TestEnv.mkurl("https", "push", "/006-push.html")
        r = TestEnv.nghttp().get(url, options=[ '-H', 'accept-push-policy: default'])
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)

    # - HEAD pushes desired
    def test_400_52(self):
        url = TestEnv.mkurl("https", "push", "/006-push.html")
        r = TestEnv.nghttp().get(url, options=[ '-H', 'accept-push-policy: head'])
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)
        assert '/006/006.css' == promises[0]["request"]["header"][":path"]
        assert "" == promises[0]["response"]["body"]
        assert 0 == len(promises[0]["response"]["body"])

    # - fast-load pushes desired
    def test_400_53(self):
        url = TestEnv.mkurl("https", "push", "/006-push.html")
        r = TestEnv.nghttp().get(url, options=[ '-H', 'accept-push-policy: fast-load'])
        assert 200 == r["response"]["status"]
        promises = r["streams"][r["response"]["id"]]["promises"]
        assert 1 == len(promises)

