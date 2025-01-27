#
# mod-h2 test suite
# check handling of trailers
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
from TestNghttp import Nghttp

def setup_module(module):
    print("setup_module: %s" % module.__name__)
    TestEnv.init()
    setup_data()
    HttpdConf().add_vhost_cgi(h2proxy_self=True).install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

def setup_data():
    s100="012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678\n"
    with open(os.path.join(TestEnv.GEN_DIR, "data-1k"), 'w') as f:
        for i in range(10):
            f.write(s100)

# The trailer tests depend on "nghttp" as no other client seems to be able to send those
# rare things.
@pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    # check if the server survives a trailer or two
    def test_202_01(self):
        url = TestEnv.mkurl("https", "cgi", "/echo.py")
        fpath = os.path.join(TestEnv.GEN_DIR, "data-1k")
        r = TestEnv.nghttp().upload(url, fpath, options=[ "--trailer", "test: 1" ])
        assert 300 > r["response"]["status"]
        assert 1000 == len(r["response"]["body"])

        r = TestEnv.nghttp().upload(url, fpath, options=[ "--trailer", "test: 1b", "--trailer", "XXX: test" ])
        assert 300 > r["response"]["status"]
        assert 1000 == len(r["response"]["body"])

    # check if the server survives a trailer without content-length
    def test_202_02(self):
        url = TestEnv.mkurl("https", "cgi", "/echo.py")
        fpath = os.path.join(TestEnv.GEN_DIR, "data-1k")
        r = TestEnv.nghttp().upload(url, fpath, options=[ "--trailer", "test: 2", "--no-content-length" ])
        assert 300 > r["response"]["status"]
        assert 1000 == len(r["response"]["body"])

    # check if echoing request headers in response from GET works
    def test_202_03(self):
        url = TestEnv.mkurl("https", "cgi", "/echohd.py?name=X")
        r = TestEnv.nghttp().get(url, options=[ "--header", "X: 3" ])
        assert 300 > r["response"]["status"]
        assert b"X: 3\n" == r["response"]["body"]

    # check if echoing request headers in response from POST works
    def test_202_03b(self):
        url = TestEnv.mkurl("https", "cgi", "/echohd.py?name=X")
        r = TestEnv.nghttp().post_name(url, "Y", options=[ "--header", "X: 3b" ])
        assert 300 > r["response"]["status"]
        assert b"X: 3b\n" == r["response"]["body"]

    # check if echoing request headers in response from POST works, but trailers are not seen
    # This is the way CGI invocation works.
    def test_202_04(self):
        url = TestEnv.mkurl("https", "cgi", "/echohd.py?name=X")
        r = TestEnv.nghttp().post_name(url, "Y", options=[ "--header", "X: 4a", "--trailer", "X: 4b" ])
        assert 300 > r["response"]["status"]
        assert b"X: 4a\n" == r["response"]["body"]

    # The h2 status handler echoes a trailer if it sees a trailer
    def test_202_05(self):
        url = TestEnv.mkurl("https", "cgi", "/.well-known/h2/state")
        fpath = os.path.join(TestEnv.GEN_DIR, "data-1k")
        r = TestEnv.nghttp().upload(url, fpath, options=[ "--trailer", "test: 2" ])
        assert 200 == r["response"]["status"]
        assert "1" == r["response"]["trailer"]["h2-trailers-in"]

    # Check that we can send and receive trailers throuh mod_proxy_http2
    def test_202_06(self):
        url = TestEnv.mkurl("https", "cgi", "/h2proxy/.well-known/h2/state")
        fpath = os.path.join(TestEnv.GEN_DIR, "data-1k")
        r = TestEnv.nghttp().upload(url, fpath, options=[ "--trailer", "test: 2" ])
        assert 200 == r["response"]["status"]
        assert "1" == r["response"]["trailer"]["h2-trailers-in"]
