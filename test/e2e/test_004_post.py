#
# mod-h2 test suite
# check POST variations
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
    HttpdConf().add_vhost_cgi().install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    # upload and GET again using curl, compare to original content
    def curl_upload_and_verify(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/upload.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)
        r = TestEnv.curl_upload(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300

        r2 = TestEnv.curl_get( r["response"]["header"]["location"])
        assert r2["rv"] == 0
        assert r2["response"]["status"] == 200 
        with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
            src = file.read()
        assert src == r2["response"]["body"]

    def test_004_01(self):
        self.curl_upload_and_verify( "data-1k", [ "--http1.1" ] )
        self.curl_upload_and_verify( "data-1k", [ "--http2" ] )

    def test_004_02(self):
        self.curl_upload_and_verify( "data-10k", [ "--http1.1" ] )
        self.curl_upload_and_verify( "data-10k", [ "--http2" ] )

    def test_004_03(self):
        self.curl_upload_and_verify( "data-100k", [ "--http1.1" ] )
        self.curl_upload_and_verify( "data-100k", [ "--http2" ] )

    def test_004_04(self):
        self.curl_upload_and_verify( "data-1m", [ "--http1.1" ] )
        self.curl_upload_and_verify( "data-1m", [ "--http2" ] )



    # POST some data using nghttp and see it echo'ed properly back
    def nghttp_post_and_verify(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/echo.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)

        r = TestEnv.nghttp().upload(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300

        with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
            src = file.read()
        assert src == r["response"]["body"]

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_004_20(self):
        self.nghttp_post_and_verify( "data-1k", [ ] )
        self.nghttp_post_and_verify( "data-10k", [ ] )
        self.nghttp_post_and_verify( "data-100k", [ ] )
        self.nghttp_post_and_verify( "data-1m", [ ] )

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_004_21(self):
        self.nghttp_post_and_verify( "data-1k", [ "--no-content-length" ] )
        self.nghttp_post_and_verify( "data-10k", [ "--no-content-length" ] )
        self.nghttp_post_and_verify( "data-100k", [ "--no-content-length" ] )
        self.nghttp_post_and_verify( "data-1m", [ "--no-content-length" ] )


    # upload and GET again using nghttp, compare to original content
    def nghttp_upload_and_verify(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/upload.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)

        r = TestEnv.nghttp().upload_file(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300
        assert r["response"]["header"]["location"]

        r2 = TestEnv.nghttp().get(r["response"]["header"]["location"])
        assert r2["rv"] == 0
        assert r2["response"]["status"] == 200 
        with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
            src = file.read()
        assert src == r2["response"]["body"]

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_004_22(self):
        self.nghttp_upload_and_verify( "data-1k", [ ] )
        self.nghttp_upload_and_verify( "data-10k", [ ] )
        self.nghttp_upload_and_verify( "data-100k", [ ] )
        self.nghttp_upload_and_verify( "data-1m", [ ] )

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_004_23(self):
        self.nghttp_upload_and_verify( "data-1k", [ "--no-content-length" ] )
        self.nghttp_upload_and_verify( "data-10k", [  "--no-content-length" ] )
        self.nghttp_upload_and_verify( "data-100k", [  "--no-content-length" ] )
        self.nghttp_upload_and_verify( "data-1m", [  "--no-content-length" ] )

