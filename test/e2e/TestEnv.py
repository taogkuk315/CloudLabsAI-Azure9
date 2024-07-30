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
from ConfigParser import SafeConfigParser
from shutil import copyfile
from urlparse import urlparse
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
    def curl_raw( cls, url, timeout, options ) :
        u = urlparse(url)
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
        args.append( url )
        r = cls.run( args )
        if r["rv"] == 0:
            lines = open(headerfile).readlines()
            exp_stat = True
            header = {}
            for line in lines:
                if exp_stat:
                    m = re.match(r'(\S+) (\d+) (.*)\r\n', line)
                    assert m
                    prev = r["response"] if "response" in r else None
                    r["response"] = {
                        "protocol"    : m.group(1), 
                        "status"      : int(m.group(2)), 
                        "description" : m.group(3),
                        "body"        : r["out"]["text"]
                    }
                    if prev:
                        r["response"]["previous"] = prev
                    exp_stat = False
                    header = {}
                elif line == "\r\n":
                    exp_stat = True
                else:
                    m = re.match(r'([^:]+):\s*(.*)\r\n', line)
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
            return r["response"]["body"].rstrip()
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
# some standard config setups
#
    @classmethod
    def vhost_cgi_install( cls ) :
        conf = HttpdConf().add_vhost_cgi().install()
    

###################################################################################################
# write apache config file
#
class HttpdConf(object):

    def __init__(self, path=None):
        if path:
            self.path = path
        else:
            self.path = os.path.join(TestEnv.GEN_DIR, "auto.conf")
        if os.path.isfile(self.path):
            os.remove(self.path)
        open(self.path, "a").write("")

    def add_line(self, line):
        open(self.path, "a").write(line + "\n")
        return self

    def add_vhost(self, port, name, aliasList=[], docRoot="htdocs", withSSL=True):
        self.start_vhost(port, name, aliasList, docRoot, withSSL)
        self.end_vhost()
        return self

    def start_vhost(self, port, name, aliasList=[], docRoot="htdocs", withSSL=True):
        f = open(self.path, "a") 
        f.write("<VirtualHost *:%s>\n" % port)
        f.write("    ServerName %s.%s\n" % (name, TestEnv.HTTP_TLD) )
        if len(aliasList) > 0:
            for alias in aliasList:
                f.write("    ServerAlias %s.%s\n" % (alias, TestEnv.HTTP_TLD) )
        f.write("    DocumentRoot %s\n\n" % docRoot)
        if withSSL:
            f.write("    SSLEngine on\n")
        return self
                  
    def end_vhost(self):
        self.add_line("</VirtualHost>\n\n")
        return self

    def install(self):
        TestEnv.install_test_conf(self.path)

    def add_vhost_test1( self ) :
        self.start_vhost( TestEnv.HTTP_PORT, "test1", aliasList=[ "www1" ], docRoot="htdocs/test1", withSSL=False
        ).add_line("      Protocols h2c http/1.1"
        ).end_vhost()
        self.start_vhost( TestEnv.HTTPS_PORT, "test1", aliasList=[ "www1" ], docRoot="htdocs/test1", withSSL=True
        ).add_line("      Protocols h2 http/1.1"
        ).add_line("      <Location /006>"
        ).add_line("        Options +Indexes"
        ).add_line("        HeaderName /006/header.html"
        ).add_line("      </Location>"
        ).end_vhost()
        return self
        
    def add_vhost_test2( self ) :
        self.start_vhost( TestEnv.HTTP_PORT, "test2", aliasList=[ "www2" ], docRoot="htdocs/test2", withSSL=False
        ).add_line("      Protocols http/1.1 h2c"
        ).end_vhost()
        self.start_vhost( TestEnv.HTTPS_PORT, "test2", aliasList=[ "www2" ], docRoot="htdocs/test2", withSSL=True
        ).add_line("      Protocols http/1.1 h2"
        ).add_line("      <Location /006>"
        ).add_line("        Options +Indexes"
        ).add_line("        HeaderName /006/header.html"
        ).add_line("      </Location>"
        ).end_vhost()
        return self

    def add_vhost_cgi( self ) :
        self.start_vhost( TestEnv.HTTPS_PORT, "cgi", aliasList=[ "cgi-alias" ], docRoot="htdocs/cgi", withSSL=True)
        self.add_line("      Protocols h2 http/1.1")
        self.add_line("      SSLOptions +StdEnvVars")
        self.add_line("      AddHandler cgi-script .py")
        self.end_vhost()
        return self

    def add_vhost_noh2( self ) :
        self.start_vhost( TestEnv.HTTPS_PORT, "noh2", aliasList=[ "noh2-alias" ], docRoot="htdocs/noh2", withSSL=True)
        self.add_line("      Protocols http/1.1")
        self.add_line("      SSLCertificateKeyFile conf/ssl/cert.pkey")
        self.add_line("      SSLCertificateFile conf/ssl/noh2.%s_cert.pem" % TestEnv.HTTP_TLD)
        self.add_line("      SSLOptions +StdEnvVars")
        self.end_vhost()
        self.start_vhost( TestEnv.HTTP_PORT, "noh2", aliasList=[ "noh2-alias" ], docRoot="htdocs/noh2", withSSL=False)
        self.add_line("      Protocols http/1.1")
        self.add_line("      SSLOptions +StdEnvVars")
        self.end_vhost()
        return self
