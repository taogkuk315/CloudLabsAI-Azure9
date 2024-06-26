#
# mod-h2 test suite
# check that we can start httpd in our environment
#

import copy
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
    #HttpdConf().add_vhost_test1().install()
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)
        assert TestEnv.apache_start() == 0

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)
        assert TestEnv.apache_stop() == 0
    
    # we expect to see the document from the generic server
    def test_001_01(self):
        data = TestEnv.get_json(TestEnv.HTTP_URL + "/alive.json", 5)
        assert data
        assert True == data["alive"]
        assert "generic" == data["host"] 

    # we expect to see the document from the generic server
    def test_001_02(self):
        data = TestEnv.get_json(TestEnv.HTTPS_URL + "/alive.json", 5)
        assert data
        assert True == data["alive"]
        assert "generic" == data["host"] 

