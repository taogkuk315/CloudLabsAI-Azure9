#
# mod-h2 test suite
# check POST use via curl
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
    setup_data()
    HttpdConf().add_vhost_cgi().install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

def setup_data():
    s100="012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678\n"
    with open(os.path.join(TestEnv.GEN_DIR, "data-1k"), 'w') as f:
        for i in range(10):
            f.write(s100)
    with open(os.path.join(TestEnv.GEN_DIR, "data-10k"), 'w') as f:
        for i in range(100):
            f.write(s100)
    with open(os.path.join(TestEnv.GEN_DIR, "data-100k"), 'w') as f:
        for i in range(1000):
            f.write(s100)
    with open(os.path.join(TestEnv.GEN_DIR, "data-1m"), 'w') as f:
        for i in range(10000):
            f.write(s100)
    
class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    def upload_and_verify(self, fname, options=None):
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

    # upload and GET again, compare to original content
    def test_004_01(self):
        self.upload_and_verify( "data-1k", [ "--http1.1" ] )
        self.upload_and_verify( "data-1k", [ "--http2" ] )

    def test_004_02(self):
        self.upload_and_verify( "data-10k", [ "--http1.1" ] )
        self.upload_and_verify( "data-10k", [ "--http2" ] )

    def test_004_03(self):
        self.upload_and_verify( "data-100k", [ "--http1.1" ] )
        self.upload_and_verify( "data-100k", [ "--http2" ] )

    def test_004_04(self):
        self.upload_and_verify( "data-1m", [ "--http1.1" ] )
        self.upload_and_verify( "data-1m", [ "--http2" ] )


