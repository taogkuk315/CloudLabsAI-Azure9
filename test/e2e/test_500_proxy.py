#
# mod-h2 test suite
# check HTTP/1.1 proxied backend
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
    HttpdConf().add_vhost_cgi( proxy_self=True ).install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    def test_500_01(self):
        url = TestEnv.mkurl("https", "cgi", "/proxy/hello.py")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/1.1" == r["response"]["json"]["protocol"]
        assert "" == r["response"]["json"]["https"]
        assert "" == r["response"]["json"]["ssl_protocol"]
        assert "" == r["response"]["json"]["h2"]
        assert "" == r["response"]["json"]["h2push"]


    # upload and GET again using curl, compare to original content
    def curl_upload_and_verify(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/proxy/upload.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)
        r = TestEnv.curl_upload(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300

        # why is the scheme wrong?
        r2 = TestEnv.curl_get(re.sub(r'http:', 'https:', r["response"]["header"]["location"]))
        assert r2["rv"] == 0
        assert r2["response"]["status"] == 200 
        with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
            src = file.read()
        assert src == r2["response"]["body"]

    def test_500_10(self):
        self.curl_upload_and_verify( "data-1k", [ "--http2" ] )
        self.curl_upload_and_verify( "data-10k", [ "--http2" ] )
        self.curl_upload_and_verify( "data-100k", [ "--http2" ] )
        self.curl_upload_and_verify( "data-1m", [ "--http2" ] )


    # POST some data using nghttp and see it echo'ed properly back
    def nghttp_post_and_verify(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/proxy/echo.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)
        r = TestEnv.nghttp().upload(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300
        with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
            src = file.read()
        #assert len(src) == len(r["response"]["body"])
        assert src == r["response"]["body"]

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_500_20(self):
        self.nghttp_post_and_verify( "data-1k", [ ] )
        self.nghttp_post_and_verify( "data-10k", [ ] )
        self.nghttp_post_and_verify( "data-100k", [ ] )
        self.nghttp_post_and_verify( "data-1m", [ ] )

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_500_21(self):
        self.nghttp_post_and_verify( "data-1k", [ "--no-content-length" ] )
        self.nghttp_post_and_verify( "data-10k", [ "--no-content-length" ] )
        self.nghttp_post_and_verify( "data-100k", [ "--no-content-length" ] )
        self.nghttp_post_and_verify( "data-1m", [ "--no-content-length" ] )


    # upload and GET again using nghttp, compare to original content
    def nghttp_upload_and_verify(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/proxy/upload.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)

        r = TestEnv.nghttp().upload_file(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300
        assert r["response"]["header"]["location"]

        # why is the scheme wrong?
        r2 = TestEnv.nghttp().get(re.sub(r'http:', 'https:', r["response"]["header"]["location"]))
        assert r2["rv"] == 0
        assert r2["response"]["status"] == 200 
        with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
            src = file.read()
        assert src == r2["response"]["body"]

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_500_22(self):
        self.nghttp_upload_and_verify( "data-1k", [ ] )
        self.nghttp_upload_and_verify( "data-10k", [ ] )
        self.nghttp_upload_and_verify( "data-100k", [ ] )
        self.nghttp_upload_and_verify( "data-1m", [ ] )

    @pytest.mark.skipif(not TestEnv.has_nghttp() or True, reason="no nghttp command available and python3 chokes in chunks")
    def test_500_23(self):
        self.nghttp_upload_and_verify( "data-1k", [ "--no-content-length" ] )
        self.nghttp_upload_and_verify( "data-10k", [  "--no-content-length" ] )
        self.nghttp_upload_and_verify( "data-100k", [  "--no-content-length" ] )
        self.nghttp_upload_and_verify( "data-1m", [  "--no-content-length" ] )

    # upload using nghttp and check returned status
    def nghttp_upload_stat(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/proxy/upload.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)

        r = TestEnv.nghttp().upload_file(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300
        assert r["response"]["header"]["location"]

    @pytest.mark.skipif(not TestEnv.has_nghttp() or True, reason="no nghttp command available and python3 chokes on chunks")
    def test_500_24(self):
        for i in range(100):
            self.nghttp_upload_stat( "data-1k", [ "--no-content-length" ] )

