#
# mod-h2 test suite
# check load with POST requests on static files
#

import copy
import re
import sys
import time
import pytest
import os

from datetime import datetime
from TestEnv import TestEnv
from TestHttpdConf import HttpdConf

def setup_module(module):
    print("setup_module: %s" % module.__name__)
    TestEnv.init()
    TestEnv.setup_data_1k_1m()
    HttpdConf().add_vhost_test1().install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    def check_h2load_ok(self, r, n):
        assert 0 == r["rv"]
        r = TestEnv.h2load_status(r)
        assert n == r["h2load"]["requests"]["total"]
        assert n == r["h2load"]["requests"]["started"]
        assert n == r["h2load"]["requests"]["done"]
        assert n == r["h2load"]["requests"]["succeeded"]
        assert n == r["h2load"]["status"]["2xx"]
        assert 0 == r["h2load"]["status"]["3xx"]
        assert 0 == r["h2load"]["status"]["4xx"]
        assert 0 == r["h2load"]["status"]["5xx"]
    
    # test POST on static file, slurped in by server
    @pytest.mark.skipif(not TestEnv.has_h2load(), reason="no h2load command available")
    def test_710_00(self):
        url = TestEnv.mkurl("https", "test1", "/index.html")
        n = 10
        m = 1
        conn = 1
        fname = "data-10k"
        args = [ TestEnv.H2LOAD, "-n", "%d" % (n), "-c", "%d" % (conn), "-m", "%d" % (m), 
            "--base-uri=https://%s:%s" % (TestEnv.HTTPD_ADDR, TestEnv.HTTPS_PORT),
            "-d", os.path.join(TestEnv.GEN_DIR, fname),  
            url ]
        r = TestEnv.run( args ) 
        self.check_h2load_ok(r, n)

    @pytest.mark.skipif(not TestEnv.has_h2load(), reason="no h2load command available")
    def test_710_01(self):
        url = TestEnv.mkurl("https", "test1", "/index.html")
        n = 1000
        m = 100
        conn = 1
        fname = "data-1k"
        args = [ TestEnv.H2LOAD, "-n", "%d" % (n), "-c", "%d" % (conn), "-m", "%d" % (m), 
            "--base-uri=https://%s:%s" % (TestEnv.HTTPD_ADDR, TestEnv.HTTPS_PORT),
            "-d", os.path.join(TestEnv.GEN_DIR, fname),  
            url ]
        r = TestEnv.run( args ) 
        self.check_h2load_ok(r, n)

    @pytest.mark.skipif(not TestEnv.has_h2load(), reason="no h2load command available")
    def test_710_02(self):
        url = TestEnv.mkurl("https", "test1", "/index.html")
        n = 100
        m = 50
        conn = 1
        fname = "data-100k"
        args = [ TestEnv.H2LOAD, "-n", "%d" % (n), "-c", "%d" % (conn), "-m", "%d" % (m), 
            "--base-uri=https://%s:%s" % (TestEnv.HTTPD_ADDR, TestEnv.HTTPS_PORT),
            "-d", os.path.join(TestEnv.GEN_DIR, fname),  
            url ]
        r = TestEnv.run( args ) 
        self.check_h2load_ok(r, n)

