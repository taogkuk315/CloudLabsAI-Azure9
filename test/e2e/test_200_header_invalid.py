#
# mod-h2 test suite
# check handling of invalid chars in headers
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

    # let the hecho.py CGI echo chars < 0x20 in field name
    # for almost all such characters, the stream gets aborted with a h2 error and 
    # there will be no http status, cr and lf are handled special
    def test_200_01(self):
        url = TestEnv.mkurl("https", "cgi", "/hecho.py")
        for x in range(1, 32):
            r = TestEnv.curl_post_data(url, "name=x%%%02xx&value=yz" % x)
            if x in [ 10 ]:
                assert 0 == r["rv"], "unexpected exit code for char 0x%02x" % x
                assert 500 == r["response"]["status"], "unexpected status for char 0x%02x" % x
            elif x in [ 13 ]:
                assert 0 == r["rv"], "unexpected exit code for char 0x%02x" % x
                assert 200 == r["response"]["status"], "unexpected status for char 0x%02x" % x
            else:
                assert 0 != r["rv"], "unexpected exit code for char 0x%02x" % x

    # let the hecho.py CGI echo chars < 0x20 in field value
    # for almost all such characters, the stream gets aborted with a h2 error and 
    # there will be no http status, cr and lf are handled special
    def test_200_02(self):
        url = TestEnv.mkurl("https", "cgi", "/hecho.py")
        for x in range(1, 32):
            if 9 != x:
                r = TestEnv.curl_post_data(url, "name=x&value=y%%%02x" % x)
                if x in [ 10, 13 ]:
                    assert 0 == r["rv"], "unexpected exit code for char 0x%02x" % x
                    assert 200 == r["response"]["status"], "unexpected status for char 0x%02x" % x
                else:
                    assert 0 != r["rv"], "unexpected exit code for char 0x%02x" % x


    # let the hecho.py CGI echo 0x10 and 0x7f in field name and value
    def test_200_03(self):
        url = TestEnv.mkurl("https", "cgi", "/hecho.py")
        for hex in [ "10", "7f" ]:
            r = TestEnv.curl_post_data(url, "name=x%%%s&value=yz" % hex)
            assert 0 != r["rv"]
            r = TestEnv.curl_post_data(url, "name=x&value=y%%%sz" % hex)
            assert 0 != r["rv"]
    

