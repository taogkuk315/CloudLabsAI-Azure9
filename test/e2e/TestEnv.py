###################################################################################################
# h2 end-to-end test environment class
#
# (c) 2018 greenbytes GmbH
###################################################################################################

import json
import pytest
import re
import os
import shutil
import subprocess
import sys
import time

from datetime import datetime
from datetime import tzinfo
from datetime import timedelta
from ConfigParser import SafeConfigParser
from httplib import HTTPConnection
from urlparse import urlparse

class TestEnv:

    @classmethod
    def init( cls ) :
        cls.config = SafeConfigParser()
        cls.config.read('config.ini')
        
        cls.GEN_DIR     = cls.config.get('global', 'gen_dir')
        cls.HTTP_PORT   = cls.config.get('httpd', 'http_port')
        cls.HTTPS_PORT  = cls.config.get('httpd', 'https_port')
        cls.TEST_DOMAIN = cls.config.get('httpd', 'test_domain')

        if not os.path.exists(cls.GEN_DIR):
            os.makedirs(cls.GEN_DIR)


    @classmethod
    def is_up( cls, url, timeout ) :
        server = urlparse(url)
        try_until = time.time() + timeout
        print("checking reachability of %s" % url)
        while time.time() < try_until:
            try:
                c = HTTPConnection(server.hostname, server.port, timeout=timeout)
                c.request('HEAD', server.path)
                resp = c.getresponse()
                c.close()
                return True
            except IOError:
                print "connect error:", sys.exc_info()[0]
                time.sleep(.2)
            except:
                print "Unexpected error:", sys.exc_info()[0]
                time.sleep(.2)
        print "Unable to contact server after %d sec" % timeout
        return False

    @classmethod
    def httpd_is_up( cls, timeout ) :
        return cls.is_up( "http://test.%s:%d" % (cls.TEST_DOMAIN, cls.HTTP_PORT), timeout )

    @classmethod
    def run( cls, args, input=None ) :
        print "execute: ", " ".join(args)
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (output, errput) = p.communicate(input)
        rv = p.wait()
        print "stderr: ", errput
        try:
            jout = json.loads(output)
        except:
            jout = None
            print "stdout: ", output
        return { 
            "rv": rv, 
            "stdout": output, 
            "stderr": errput,
            "jout" : jout 
        }

