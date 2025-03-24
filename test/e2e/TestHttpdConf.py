###################################################################################################
# httpd test configuration generator
#
# (c) 2019 greenbytes GmbH
###################################################################################################

import os


class HttpdConf(object):

    def __init__(self, env, path=None):
        self.env = env
        if path:
            self.path = path
        else:
            self.path = os.path.join(env.GEN_DIR, "auto.conf")
        if os.path.isfile(self.path):
            os.remove(self.path)
        open(self.path, "a").write("""
        H2MinWorkers 4
        H2MaxWorkers 32
        LogLevel http2:info h2test:trace2 proxy_http2:info\n
        """)

    def add_line(self, line):
        open(self.path, "a").write(line + "\n")
        return self

    def add_vhost(self, port, name, aliasList=None, docRoot="htdocs", withSSL=True):
        self.start_vhost(port, name, aliasList, docRoot, withSSL)
        self.end_vhost()
        return self

    def start_vhost(self, port, name, aliasList=None, docRoot="htdocs", withSSL=True):
        f = open(self.path, "a") 
        f.write("<VirtualHost *:%s>\n" % port)
        f.write("    ServerName %s.%s\n" % (name, self.env.HTTP_TLD) )
        if aliasList:
            for alias in aliasList:
                f.write("    ServerAlias %s.%s\n" % (alias, self.env.HTTP_TLD))
        f.write("    DocumentRoot %s\n\n" % docRoot)
        if withSSL:
            f.write("    SSLEngine on\n")
        return self
                  
    def end_vhost(self):
        self.add_line("</VirtualHost>\n\n")
        return self

    def install(self):
        self.env.install_test_conf(self.path)

    def add_proxies(self, host, proxy_self=False, h2proxy_self=False):
        if proxy_self or h2proxy_self:
            self.add_line("      ProxyPreserveHost on")
        if proxy_self:
            self.add_line("      ProxyPass \"/proxy/\" \"http://127.0.0.1:%s/\"" % self.env.HTTP_PORT)
            self.add_line("      ProxyPassReverse \"/proxy/\" \"http://%s.%s:%s/\"" 
                % (host, self.env.HTTP_TLD, self.env.HTTP_PORT))
        if h2proxy_self:
            self.add_line("      ProxyPass \"/h2proxy/\" \"h2://127.0.0.1:%s/\"" % self.env.HTTPS_PORT)
            self.add_line("      ProxyPassReverse \"/h2proxy/\" \"https://%s.%s:%s/\""
                          % (host, self.env.HTTP_TLD, self.env.HTTPS_PORT))
        return self
    
    def add_vhost_test1(self, proxy_self=False, h2proxy_self=False) :
        self.start_vhost(
            self.env.HTTP_PORT, "test1", aliasList=["www1"], docRoot="htdocs/test1", withSSL=False
        ).add_line(
            "      Protocols h2c http/1.1"
        ).end_vhost()
        self.start_vhost(
            self.env.HTTPS_PORT, "test1", aliasList=["www1"], docRoot="htdocs/test1", withSSL=True
        ).add_line("      Protocols h2 http/1.1"
        ).add_line("      <Location /006>"
        ).add_line("        Options +Indexes"
        ).add_line("        HeaderName /006/header.html"
        ).add_line("      </Location>"
        ).add_proxies( "test1", proxy_self, h2proxy_self 
        ).end_vhost()
        return self
        
    def add_vhost_test2(self):
        self.start_vhost(self.env.HTTP_PORT, "test2", aliasList=["www2"], docRoot="htdocs/test2", withSSL=False
        ).add_line("      Protocols http/1.1 h2c"
        ).end_vhost()
        self.start_vhost( self.env.HTTPS_PORT, "test2", aliasList=["www2"], docRoot="htdocs/test2", withSSL=True
        ).add_line("      Protocols http/1.1 h2"
        ).add_line("      <Location /006>"
        ).add_line("        Options +Indexes"
        ).add_line("        HeaderName /006/header.html"
        ).add_line("      </Location>"
        ).end_vhost()
        return self

    def add_vhost_cgi(self, proxy_self=False, h2proxy_self=False):
        if proxy_self:
            self.add_proxy_setup()
        if h2proxy_self:
            self.add_line("      SSLProxyEngine on")
            self.add_line("      SSLProxyCheckPeerName off")
        self.start_vhost(self.env.HTTPS_PORT, "cgi", aliasList=["cgi-alias"], docRoot="htdocs/cgi", withSSL=True)
        self.add_line("      Protocols h2 http/1.1")
        self.add_line("      SSLOptions +StdEnvVars")
        self.add_line("      AddHandler cgi-script .py")
        self.add_line("      <Location \"/.well-known/h2/state\">")
        self.add_line("          SetHandler http2-status")
        self.add_line("      </Location>")
        self.add_proxies("cgi", proxy_self, h2proxy_self)
        self.add_line("      <Location \"/h2test/echo\">")
        self.add_line("          SetHandler h2test-echo")
        self.add_line("      </Location>")
        self.end_vhost()
        self.start_vhost(self.env.HTTP_PORT, "cgi", aliasList=["cgi-alias"], docRoot="htdocs/cgi", withSSL=False)
        self.add_line("      AddHandler cgi-script .py")
        self.end_vhost()
        self.add_line("      LogLevel proxy:info")
        self.add_line("      LogLevel proxy_http:info")
        return self

    def add_vhost_noh2(self):
        self.start_vhost(self.env.HTTPS_PORT, "noh2", aliasList=["noh2-alias"], docRoot="htdocs/noh2", withSSL=True)
        self.add_line("      Protocols http/1.1")
        self.add_line("      SSLCertificateKeyFile conf/ssl/cert.pkey")
        self.add_line("      SSLCertificateFile conf/ssl/noh2.%s_cert.pem" % self.env.HTTP_TLD)
        self.add_line("      SSLOptions +StdEnvVars")
        self.end_vhost()
        self.start_vhost(self.env.HTTP_PORT, "noh2", aliasList=["noh2-alias"], docRoot="htdocs/noh2", withSSL=False)
        self.add_line("      Protocols http/1.1")
        self.add_line("      SSLOptions +StdEnvVars")
        self.end_vhost()
        return self

    def add_proxy_setup(self):
        self.add_line("ProxyStatus on")
        self.add_line("ProxyTimeout 5")
        self.add_line("SSLProxyEngine on")
        self.add_line("SSLProxyVerify none")
        return self
