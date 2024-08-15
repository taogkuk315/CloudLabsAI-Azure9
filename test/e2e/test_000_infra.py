#
# mod-h2 test suite
# check that our test infrastructure is sane
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
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)

class TestStore:

    def test_000_00(self):
        assert 1 == 1

