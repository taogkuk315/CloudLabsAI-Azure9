#
# mod-h2 test suite
# curl get
#

import copy
import re
import sys
import time
import pytest

from datetime import datetime
from TestEnv import TestEnv

def setup_module(module):
    print("setup_module: %s" % module.__name__)
    TestEnv.init()
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)

def md_name(md):
    return md['name']

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)
 
    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)

    def test_001_01(self):
        print "nop"
