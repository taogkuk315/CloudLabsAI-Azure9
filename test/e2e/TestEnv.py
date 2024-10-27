###################################################################################################
# h2 end-to-end test environment class
#
# (c) 2019 greenbytes GmbH
###################################################################################################

import json
import pytest
import re
import os
import shutil
import subprocess
import sys
import string
import time
import requests

from datetime import datetime
from datetime import tzinfo
from datetime import timedelta
from configparser import SafeConfigParser
from shutil import copyfile
from urllib.parse import urlparse
from TestNghttp import Nghttp

class TestEnv:

    initialized = False
    
    @classmethod
    def init( cls ) :
        if TestEnv.initialized:
            return
        cls.config = SafeConfigParser()
        cls.config.read('config.ini')
        
        cls.PREFIX      = cls.config.get('global', 'prefix')
        cls.GEN_DIR     = cls.config.get('global', 'gen_dir')
        cls.WEBROOT     = cls.config.get('global', 'server_dir')
        cls.CURL        = cls.config.get('global', 'curl_bin')
        cls.TEST_DIR    = cls.config.get('global', 'test_dir')
        cls.NGHTTP      = cls.config.get('global', 'nghttp')
        cls.H2LOAD      = cls.config.get('global', 'h2load')

        cls.HTTP_PORT   = cls.config.get('httpd', 'http_port')
        cls.HTTPS_PORT  = cls.config.get('httpd', 'https_port')
        cls.HTTP_TLD    = cls.config.get('httpd', 'http_tld')

        cls.APACHECTL  = os.path.join(cls.PREFIX, 'bin', 'apachectl')

        cls.HTTPD_ADDR = "127.0.0.1"
        cls.HTTP_URL   = "http://" + cls.HTTPD_ADDR + ":" + cls.HTTP_PORT
        cls.HTTPS_URL  = "https://" + cls.HTTPD_ADDR + ":" + cls.HTTPS_PORT
        
        cls.HTTPD_CONF_DIR = os.path.join(cls.WEBROOT, "conf")
        cls.HTTPD_DOCS_DIR = os.path.join(cls.WEBROOT, "htdocs")
        cls.HTTPD_TEST_CONF = os.path.join(cls.HTTPD_CONF_DIR, "test.conf")
        cls.E2E_DIR    = os.path.join(cls.TEST_DIR, "e2e")

        cls.VERIFY_CERTIFICATES = False
        
        if not os.path.exists(cls.GEN_DIR):
            os.makedirs(cls.GEN_DIR)
        
        TestEnv.initialized = True

###################################################################################################
# check features
    @classmethod
    def has_h2load( cls ) :
        cls.init()
        return cls.H2LOAD != ""

    @classmethod
    def has_nghttp( cls ) :
        cls.init()
        return cls.NGHTTP != ""


###################################################################################################
# path construction
#
    @classmethod
    def mkpath( cls, path ) :
        if not os.path.exists(path):
            return os.makedirs(path)


    @classmethod
    def e2e_src( cls, path ) :
        return os.path.join(cls.E2E_DIR, path)

###################################################################################################
# command execution
#
    @classmethod
    def run( cls, args, input=None ) :
        print("execute: %s" % " ".join(args))
        p = subprocess.run(args, capture_output=True)
        rv = p.returncode
        print("stderr: %s" % p.stderr)
        try:
            jout = json.loads(p.stdout)
        except:
            jout = None
            print("stdout: %s" % p.stdout)
        return { 
            "rv": rv,
            "out" : {
                "raw" : p.stdout,
                "text" : p.stdout.decode('utf-8'),
                "err" : p.stderr.decode('utf-8'),
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
                print("connect error: %s" % sys.exc_info()[0])
                time.sleep(.2)
            except:
                print("Unexpected error: %s" % sys.exc_info()[0])
                time.sleep(.2)
        print("Unable to contact '%s' after %d sec" % (url, timeout))
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
        print("Server still responding after %d sec" % timeout)
        return False

###################################################################################################
# apachectl
#
    @classmethod
    def apachectl( cls, cmd, conf=None, check_live=True ) :
        if conf:
            cls.install_test_conf(conf)
        args = [cls.APACHECTL, "-d", cls.WEBROOT, "-k", cmd]
        print("execute: %s" % " ".join(args))
        cls.apachectl_stderr = ""
        p = subprocess.run(args, capture_output=True, text=True)
        sys.stderr.write(p.stderr)
        rv = p.returncode
        if rv == 0:
            if check_live:
                rv = 0 if cls.is_live(cls.HTTP_URL, 10) else -1
            else:
                rv = 0 if cls.is_dead(cls.HTTP_URL, 10) else -1
                print("waited for a apache.is_dead, rv=%d" % rv)
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
            print("check, if dead: %s" % cls.HTTPD_CHECK_URL)
            return 0 if cls.is_dead(cls.HTTPD_CHECK_URL, 5) else -1
        return rv
        
    @classmethod
    def install_test_conf( cls, conf=None) :
        if conf is None:
            conf_src = os.path.join("conf", "test.conf")
        elif os.path.isabs(conf):
            conf_src = conf
        else:
            conf_src = os.path.join("data", conf + ".conf")
        copyfile(conf_src, cls.HTTPD_TEST_CONF)

###################################################################################################
# curl
#
    @classmethod
    def curl_raw( cls, urls, timeout, options ) :
        if not isinstance(urls, list):
            urls = [ urls ]
        u = urlparse(urls[0])
        headerfile = ("%s/curl.headers" % cls.GEN_DIR)
        if os.path.isfile(headerfile):
            os.remove(headerfile)

        args = [ 
            cls.CURL,
            "-ks", "-D", headerfile, 
            "--resolve", ("%s:%s:%s" % (u.hostname, u.port, cls.HTTPD_ADDR)),
            "--connect-timeout", ("%d" % timeout) 
        ]
        if options:
            args.extend(options)
        args += urls
        r = cls.run( args )
        if r["rv"] == 0:
            lines = open(headerfile).readlines()
            exp_stat = True
            header = {}
            for line in lines:
                if exp_stat:
                    print("reading 1st response line: %s" % line)
                    m = re.match(r'^(\S+) (\d+) (.*)$', line)
                    assert m
                    prev = r["response"] if "response" in r else None
                    r["response"] = {
                        "protocol"    : m.group(1), 
                        "status"      : int(m.group(2)), 
                        "description" : m.group(3),
                        "body"        : r["out"]["raw"]
                    }
                    if prev:
                        r["response"]["previous"] = prev
                    exp_stat = False
                    header = {}
                elif re.match(r'^$', line):
                    exp_stat = True
                else:
                    print("reading header line: %s" % line)
                    m = re.match(r'^([^:]+):\s*(.*)$', line)
                    assert m
                    header[ m.group(1).lower() ] = m.group(2)
            r["response"]["header"] = header
            if r["out"]["json"]:
                r["response"]["json"] = r["out"]["json"] 
        return r

    @classmethod
    def curl_get( cls, url, timeout=5, options=None ) :
        return cls.curl_raw( url, timeout=timeout, options=options )

    @classmethod
    def curl_upload( cls, url, fpath, timeout=5, options=None ) :
        fname = os.path.basename(fpath)
        if not options:
            options = []
        options.extend([
            "--form", ("file=@%s" % (fpath))
        ])
        return cls.curl_raw( url, timeout, options )

    @classmethod
    def curl_post_data( cls, url, data="", timeout=5, options=None ) :
        if not options:
            options = []
        options.extend([ "--data", "%s" % data ])
        return cls.curl_raw( url, timeout, options )

    @classmethod
    def curl_protocol_version( cls, url, timeout=5, options=None ) :
        if not options:
            options = []
        options.extend([ "-w", "%{http_version}\n", "-o", "/dev/null" ])
        r = cls.curl_raw( url, timeout=timeout, options=options )
        if r["rv"] == 0 and "response" in r:
            return r["response"]["body"].decode('utf-8').rstrip()
        return -1
        
###################################################################################################
# nghttp
#
    @classmethod
    def nghttp( cls ) :
        return Nghttp( cls.NGHTTP, connect_addr=cls.HTTPD_ADDR, tmp_dir=cls.GEN_DIR )


###################################################################################################
# h2load
#
    @classmethod
    def h2load_status( cls, run ) :
        m = re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout', run["out"]["text"])
        if m:
            run["h2load"] = {
                "requests" : {
                    "total" : int(m.group(1)),
                    "started" : int(m.group(2)),
                    "done" : int(m.group(3)),
                    "succeeded" : int(m.group(4)) 
                }
            }
            m = re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx', run["out"]["text"])
            if m:
                run["h2load"]["status"] = {
                    "2xx" : int(m.group(1)),
                    "3xx" : int(m.group(2)),
                    "4xx" : int(m.group(3)),
                    "5xx" : int(m.group(4))
                }
        return run


###################################################################################################
# generate some test data
#
    @classmethod
    def setup_data_1k_1m( cls ):
        s100="012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678\n"
        with open(os.path.join(cls.GEN_DIR, "data-1k"), 'w') as f:
            for i in range(10):
                f.write(s100)
        with open(os.path.join(cls.GEN_DIR, "data-10k"), 'w') as f:
            for i in range(100):
                f.write(s100)
        with open(os.path.join(cls.GEN_DIR, "data-100k"), 'w') as f:
            for i in range(1000):
                f.write(s100)
        with open(os.path.join(cls.GEN_DIR, "data-1m"), 'w') as f:
            for i in range(10000):
                f.write(s100)
        

