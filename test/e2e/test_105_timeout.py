#
# mod-h2 test suite
# check HTTP/2 timeout behaviour
#

import copy
import re
import sys
import socket
import time
import pytest

from datetime import datetime
from TestEnv import TestEnv
from TestHttpdConf import HttpdConf

def setup_module(module):
    print("setup_module: %s" % module.__name__)
    TestEnv.init()

        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    # Check that base servers 'Timeout' setting is observed on SSL handshake
    def test_105_01(self):
        conf = HttpdConf()
        conf.add_line("""
            Timeout 2
            """)
        conf.add_vhost_cgi()
        conf.install()
        assert TestEnv.apache_restart() == 0
        host = 'localhost'
        # read with a longer timeout than the server 
        sock = socket.create_connection((host, int(TestEnv.HTTPS_PORT)))
        try:
            sock.settimeout(2.5)
            buff = sock.recv(1024)
            assert buff == b''
        except Exception as ex:
            print(f"server did not close in time: {ex}")
            assert False
        sock.close()
        # read with a shorter timeout than the server 
        sock = socket.create_connection((host, int(TestEnv.HTTPS_PORT)))
        try:
            sock.settimeout(0.5)
            buff = sock.recv(1024)
            assert False
        except Exception as ex:
            print(f"as expected: {ex}")
        sock.close()

    # Check that mod_reqtimeout handshake setting takes effect
    def test_105_02(self):
        conf = HttpdConf()
        conf.add_line("""
            Timeout 10
            RequestReadTimeout handshake=2 header=5 body=10
            """)
        conf.add_vhost_cgi()
        conf.install()
        assert TestEnv.apache_restart() == 0
        host = 'localhost'
        # read with a longer timeout than the server 
        sock = socket.create_connection((host, int(TestEnv.HTTPS_PORT)))
        try:
            sock.settimeout(2.5)
            buff = sock.recv(1024)
            assert buff == b''
        except Exception as ex:
            print(f"server did not close in time: {ex}")
            assert False
        sock.close()
        # read with a shorter timeout than the server 
        sock = socket.create_connection((host, int(TestEnv.HTTPS_PORT)))
        try:
            sock.settimeout(0.5)
            buff = sock.recv(1024)
            assert False
        except Exception as ex:
            print(f"as expected: {ex}")
        sock.close()

    # Check that mod_reqtimeout handshake setting do no longer apply to handshaked 
    # connections. See <https://github.com/icing/mod_h2/issues/196>.
    def test_105_03(self):
        conf = HttpdConf()
        conf.add_line("""
            Timeout 10
            RequestReadTimeout handshake=1 header=5 body=10
            """)
        conf.add_vhost_cgi()
        conf.install()
        assert TestEnv.apache_restart() == 0
        url = TestEnv.mkurl("https", "cgi", "/necho.py")
        r = TestEnv.curl_get(url, 5, [ "-vvv",  
            "-F", ("count=%d" % (100)), 
            "-F", ("text=%s" % ("abcdefghijklmnopqrstuvwxyz")),
            "-F", ("wait1=%f" % (1.5)),
        ])
        assert 200 == r["response"]["status"]
