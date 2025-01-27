#
# mod-h2 test status page
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
    
    def test_005_01(self):
        url = TestEnv.mkurl("https", "cgi", "/.well-known/h2/state")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        st = r["response"]["json"]
        
        # remove some parts that are very dependant on client/lib versions
        # or connection time etc.
        del st["settings"]["SETTINGS_INITIAL_WINDOW_SIZE"]
        del st["peerSettings"]["SETTINGS_INITIAL_WINDOW_SIZE"]
        del st["streams"]["1"]["created"]
        del st["streams"]["1"]["flowOut"]
        del st["stats"]["in"]["frames"]
        del st["stats"]["in"]["octets"]
        del st["stats"]["out"]["frames"]
        del st["stats"]["out"]["octets"]
        del st["connFlowOut"]
        
        assert st == {
            "version" : "draft-01",
            "settings" : {
                "SETTINGS_MAX_CONCURRENT_STREAMS": 100,
                "SETTINGS_MAX_FRAME_SIZE": 16384,
                "SETTINGS_ENABLE_PUSH": 0
            },
            "peerSettings" : {
                "SETTINGS_MAX_CONCURRENT_STREAMS": 100,
                "SETTINGS_MAX_FRAME_SIZE": 16384,
                "SETTINGS_ENABLE_PUSH": 0,
                "SETTINGS_HEADER_TABLE_SIZE": 4096,
                "SETTINGS_MAX_HEADER_LIST_SIZE": -1
            },
            "connFlowIn": 2147483647,
            "sentGoAway": 0,
            "streams": {
                "1": {
                    "state": "HALF_CLOSED_REMOTE",
                    "flowIn": 65535,
                    "dataIn": 0,
                    "dataOut": 0
                }
            },
            "stats": {
                "in": {
                    "requests": 1,
                    "resets": 0, 
                },
                "out": {
                    "responses": 0,
                },
                "push": {
                    "cacheDigest": "AQg",
                    "promises": 0,
                    "submits": 0,
                    "resets": 0
                }
            }
        }
