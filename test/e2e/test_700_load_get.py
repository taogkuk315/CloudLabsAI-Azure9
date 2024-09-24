#
# mod-h2 test suite
# check load with GET requests
#

import copy
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
    HttpdConf().add_vhost_cgi().add_vhost_test1().install()
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
    
    # test load on cgi script, single connection, different sizes
    @pytest.mark.skipif(not TestEnv.has_h2load(), reason="no h2load command available")
    @pytest.mark.parametrize("start", [
        1000, 80000
    ])
    def test_700_10(self, start):
        text = "X"
        chunk = 32
        for n in range(0, 5):
            args = [ TestEnv.H2LOAD, "-n", "%d" % chunk, "-c", "1", "-m", "10", "--base-uri=https://%s:%s" % (TestEnv.HTTPD_ADDR, TestEnv.HTTPS_PORT) ]
            for i in range(0, chunk):
                args.append( TestEnv.mkurl("https", "cgi", ("/mnot164.py?count=%d&text=%s" % (start+(n*chunk)+i, text))) )
            r = TestEnv.run( args ) 
            self.check_h2load_ok(r, chunk)

    # test load on cgi script, single connection
    @pytest.mark.skipif(not TestEnv.has_h2load(), reason="no h2load command available")
    @pytest.mark.parametrize("conns", [
        1, 2, 16, 32
    ])
    def test_700_11(self, conns):
        text = "X"
        start = 1200
        chunk = 64
        for n in range(0, 5):
            args = [ TestEnv.H2LOAD, "-n", "%d" % chunk, "-c", "%d" % conns, "-m", "10", "--base-uri=https://%s:%s" % (TestEnv.HTTPD_ADDR, TestEnv.HTTPS_PORT) ]
            for i in range(0, chunk):
                args.append( TestEnv.mkurl("https", "cgi", ("/mnot164.py?count=%d&text=%s" % (start+(n*chunk)+i, text))) )
            r = TestEnv.run( args ) 
            self.check_h2load_ok(r, chunk)

