###################################################################################################
# httpd test configuration generator
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

from TestEnv import TestEnv

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

    def add_vhost_cgi( self, proxy_self=False ) :
        if proxy_self:
            self.add_proxy_setup()
        self.start_vhost( TestEnv.HTTPS_PORT, "cgi", aliasList=[ "cgi-alias" ], docRoot="htdocs/cgi", withSSL=True)
        self.add_line("      Protocols h2 http/1.1")
        self.add_line("      SSLOptions +StdEnvVars")
        self.add_line("      AddHandler cgi-script .py")
        self.add_line("      <Location \"/.well-known/h2/state\">")
        self.add_line("          SetHandler http2-status")
        self.add_line("      </Location>")
        if proxy_self:
            self.add_line("      ProxyPreserveHost on")
            self.add_line("      ProxyPass \"/proxy\" \"http://127.0.0.1:%s/\"" % (TestEnv.HTTP_PORT))
            self.add_line("      ProxyPassReverse \"/proxy\" \"http://%s.%s:%s/\"" 
                % ("cgi", TestEnv.HTTP_TLD, TestEnv.HTTP_PORT))
        self.end_vhost()
        self.start_vhost( TestEnv.HTTP_PORT, "cgi", aliasList=[ "cgi-alias" ], docRoot="htdocs/cgi", withSSL=False)
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

    def add_proxy_setup( self ) :
        self.add_line("ProxyStatus on")
        self.add_line("ProxyTimeout 5")
        self.add_line("SSLProxyEngine on")
        self.add_line("SSLProxyVerify none")
        return self

