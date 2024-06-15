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
import requests

from datetime import datetime
from datetime import tzinfo
from datetime import timedelta
from ConfigParser import SafeConfigParser
from urlparse import urlparse

class TestEnv:

    @classmethod
    def init( cls ) :
        cls.config = SafeConfigParser()
        cls.config.read('config.ini')
        
        cls.PREFIX      = cls.config.get('global', 'prefix')
        cls.GEN_DIR     = cls.config.get('global', 'gen_dir')
        cls.WEBROOT     = cls.config.get('global', 'server_dir')
        cls.CURL        = cls.config.get('global', 'curl_bin')

        cls.HTTP_PORT   = cls.config.get('httpd', 'http_port')
        cls.HTTPS_PORT  = cls.config.get('httpd', 'https_port')
        cls.HTTP_TLD    = cls.config.get('httpd', 'http_tld')

        cls.APACHECTL = os.path.join(cls.PREFIX, 'bin', 'apachectl')

        cls.HTTPD_ADDR = "127.0.0.1"
        cls.HTTP_URL = "http://" + cls.HTTPD_ADDR + ":" + cls.HTTP_PORT
        cls.HTTPS_URL = "https://" + cls.HTTPD_ADDR + ":" + cls.HTTPS_PORT

        cls.VERIFY_CERTIFICATES = False
        
        if not os.path.exists(cls.GEN_DIR):
            os.makedirs(cls.GEN_DIR)


    @classmethod
    def is_up( cls, url, timeout ) :
        u = urlparse(url)
        try_until = time.time() + timeout
        print("checking reachability of %s" % url)
        while time.time() < try_until:
            try:
                c = HTTPConnection(u.hostname, u.port, timeout=timeout)
                c.request('HEAD', u.path)
                resp = c.getresponse()
                c.close()
                return True
            except IOError:
                print ("connect error: %s" % sys.exc_info()[0])
                time.sleep(.2)
            except:
                print ("Unexpected error: %s" % sys.exc_info()[0])
                time.sleep(.2)
        print ("Unable to contact server after %d sec" % timeout)
        return False

    @classmethod
    def httpd_is_up( cls, timeout ) :
        return cls.is_up( "http://test.%s:%d" % (cls.TEST_DOMAIN, cls.HTTP_PORT), timeout )

    @classmethod
    def run( cls, args, input=None ) :
        print ("execute: %s" % " ".join(args))
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (output, errput) = p.communicate(input)
        rv = p.wait()
        print ("stderr: %s" % errput)
        try:
            jout = json.loads(output)
        except:
            jout = None
            print ("stdout: %s" % output)
        return { 
            "rv": rv,
            "out" : {
                "text" : output,
                "err" : errput,
                "json" : jout
            } 
        }

    @classmethod
    def mkurl( cls, scheme, hostname, path='/' ) :
        port = cls.HTTPS_PORT if scheme == 'https' else cls.HTTP_PORT
        return "%s://%s.%s:%s%s" % (scheme, hostname, cls.HTTP_TLD, port, path)

###################################################################################################
# http methods
#
    @classmethod
    def is_live( cls, url, timeout ) :
        s = requests.Session()
        try_until = time.time() + timeout
        print("checking reachability of %s" % url)
        while time.time() < try_until:
            try:
                req = requests.Request('HEAD', url).prepare()
                resp = s.send(req, verify=cls.VERIFY_CERTIFICATES, timeout=timeout)
                return True
            except IOError:
                print ("connect error: %s" % sys.exc_info()[0])
                time.sleep(.2)
            except:
                print ("Unexpected error: %s" % sys.exc_info()[0])
                time.sleep(.2)
        print ("Unable to contact '%s' after %d sec" % (url, timeout))
        return False

    @classmethod
    def is_dead( cls, url, timeout ) :
        s = requests.Session()
        try_until = time.time() + timeout
        print("checking reachability of %s" % url)
        while time.time() < try_until:
            try:
                req = requests.Request('HEAD', url).prepare()
                resp = s.send(req, verify=cls.VERIFY_CERTIFICATES, timeout=timeout)
                time.sleep(.2)
            except IOError:
                return True
            except:
                return True
        print ("Server still responding after %d sec" % timeout)
        return False

    @classmethod
    def get_json( cls, url, timeout ) :
        data = cls.get_plain( url, timeout )
        if data:
            return json.loads(data)
        return None

    @classmethod
    def get_plain( cls, url, timeout ) :
        s = requests.Session()
        try_until = time.time() + timeout
        while time.time() < try_until:
            try:
                req = requests.Request('GET', url).prepare()
                resp = s.send(req, verify=cls.VERIFY_CERTIFICATES, timeout=timeout)
                return resp.text
            except IOError:
                print ("connect error: %s" % sys.exc_info()[0])
                time.sleep(.1)
            except:
                print ("Unexpected error: %s" % sys.exc_info()[0])
                return None
        print ("Unable to contact server after %d sec" % timeout)
        return None

###################################################################################################
# apachectl
#
    @classmethod
    def apachectl( cls, cmd, conf=None, check_live=True ) :
        if conf:
            cls.install_test_conf(conf)
        args = [cls.APACHECTL, "-d", cls.WEBROOT, "-k", cmd]
        print ("execute: %s" % " ".join(args))
        cls.apachectl_stderr = ""
        p = subprocess.Popen(args, stderr=subprocess.PIPE)
        (output, cls.apachectl_stderr) = p.communicate()
        sys.stderr.write(cls.apachectl_stderr)
        rv = p.wait()
        if rv == 0:
            if check_live:
                rv = 0 if cls.is_live(cls.HTTP_URL, 10) else -1
            else:
                rv = 0 if cls.is_dead(cls.HTTP_URL, 10) else -1
                print ("waited for a apache.is_dead, rv=%d" % rv)
        return rv

    @classmethod
    def apache_restart( cls ) :
        return cls.apachectl( "graceful" )
        
    @classmethod
    def apache_start( cls ) :
        return cls.apachectl( "start" )

    @classmethod
    def apache_stop( cls ) :
        return cls.apachectl( "stop", check_live=False )

    @classmethod
    def apache_fail( cls ) :
        rv = cls.apachectl( "graceful", check_live=False )
        if rv != 0:
            print ("check, if dead: %s" % cls.HTTPD_CHECK_URL)
            return 0 if cls.is_dead(cls.HTTPD_CHECK_URL, 5) else -1
        return rv
        
###################################################################################################
# curl
#
    @classmethod
    def curl_raw( cls, args ) :
        return cls.run( [ cls.CURL ] + args )

    @classmethod
    def curl_get( cls, url, timeout=5 ) :
        u = urlparse(url)
        headerfile = ("%s/curl.headers" % cls.GEN_DIR)
        r = cls.curl_raw([ 
            "-ks", "-D", headerfile, 
            "--resolve", ("%s:%s:%s" % (u.hostname, u.port, cls.HTTPD_ADDR)),
            "--connect-timeout", ("%d" % timeout), 
            url 
        ])
        if r["rv"] == 0:
            lines = open(headerfile).readlines()
            m = re.match(r'(\S+) (\d+) (.*)', lines[0])
            if m:
                r["response"] = {
                    "protocol"    : m.group(1), 
                    "status"      : int(m.group(2)), 
                    "description" : m.group(3),
                    "body"        : r["out"]["text"]
                }
                if r["out"]["json"]:
                    r["response"]["json"] = r["out"]["json"] 
        return r
