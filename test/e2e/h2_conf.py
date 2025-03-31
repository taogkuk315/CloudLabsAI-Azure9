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
            self.path = os.path.join(env.gen_dir, "auto.conf")
        if os.path.isfile(self.path):
            os.remove(self.path)
        open(self.path, "a").write(f"""
        LoadModule mpm_{env.mpm_type}_module  \"{env.libexec_dir}/mod_mpm_{env.mpm_type}.so\"
        
        H2MinWorkers 4
        H2MaxWorkers 32
        LogLevel http2:info h2test:trace2 proxy_http2:info
        """)

    def add_line(self, line):
        open(self.path, "a").write(line + "\n")
        return self

    def add_vhost(self, port, name, aliases=None, doc_root="htdocs", with_ssl=True):
        self.start_vhost(port, name, aliases, doc_root, with_ssl)
        self.end_vhost()
        return self

    def start_vhost(self, port, name, aliases=None, doc_root="htdocs", with_ssl=True):
        f = open(self.path, "a") 
        f.write("<VirtualHost *:%s>\n" % port)
        f.write("    ServerName %s.%s\n" % (name, self.env.http_tld))
        if aliases:
            for alias in aliases:
                f.write("    ServerAlias %s.%s\n" % (alias, self.env.http_tld))
        f.write("    DocumentRoot %s\n\n" % doc_root)
        if with_ssl:
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
            self.add_line(f"""
                ProxyPass /proxy/ http://127.0.0.1:{self.env.http_port}/
                ProxyPassReverse /proxy/ http://{host}.{self.env.http_tld}:{self.env.http_port}/
            """)
        if h2proxy_self:
            self.add_line(f"""
                ProxyPass /h2proxy/ h2://127.0.0.1:{self.env.https_port}/
                ProxyPassReverse /h2proxy/ https://{host}.{self.env.http_tld}:self.env.https_port/
            """)
        return self
    
    def add_vhost_test1(self, proxy_self=False, h2proxy_self=False):
        self.start_vhost(
            self.env.http_port, "test1", aliases=["www1"], doc_root="htdocs/test1", with_ssl=False
        ).add_line(
            "      Protocols h2c http/1.1"
        ).end_vhost()
        self.start_vhost(
            self.env.https_port, "test1", aliases=["www1"], doc_root="htdocs/test1", with_ssl=True)
        self.add_line("""
            Protocols h2 http/1.1
            <Location /006>
                Options +Indexes
                HeaderName /006/header.html
            </Location>""")
        self.add_proxies("test1", proxy_self, h2proxy_self)
        self.end_vhost()
        return self
        
    def add_vhost_test2(self):
        self.start_vhost(self.env.http_port, "test2", aliases=["www2"], doc_root="htdocs/test2", with_ssl=False)
        self.add_line("      Protocols http/1.1 h2c")
        self.end_vhost()
        self.start_vhost(self.env.https_port, "test2", aliases=["www2"], doc_root="htdocs/test2", with_ssl=True)
        self.add_line("""
            Protocols http/1.1 h2
            <Location /006>
                Options +Indexes
                HeaderName /006/header.html
            </Location>""")
        self.end_vhost()
        return self

    def add_vhost_cgi(self, proxy_self=False, h2proxy_self=False):
        if proxy_self:
            self.add_proxy_setup()
        if h2proxy_self:
            self.add_line("      SSLProxyEngine on")
            self.add_line("      SSLProxyCheckPeerName off")
        self.start_vhost(self.env.https_port, "cgi", aliases=["cgi-alias"], doc_root="htdocs/cgi", with_ssl=True)
        self.add_line("""
            Protocols h2 http/1.1
            SSLOptions +StdEnvVars
            AddHandler cgi-script .py
            <Location \"/.well-known/h2/state\">
                SetHandler http2-status
            </Location>""")
        self.add_proxies("cgi", proxy_self, h2proxy_self)
        self.add_line("      <Location \"/h2test/echo\">")
        self.add_line("          SetHandler h2test-echo")
        self.add_line("      </Location>")
        self.end_vhost()
        self.start_vhost(self.env.http_port, "cgi", aliases=["cgi-alias"], doc_root="htdocs/cgi", with_ssl=False)
        self.add_line("      AddHandler cgi-script .py")
        self.end_vhost()
        self.add_line("      LogLevel proxy:info")
        self.add_line("      LogLevel proxy_http:info")
        return self

    def add_vhost_noh2(self):
        self.start_vhost(self.env.https_port, "noh2", aliases=["noh2-alias"], doc_root="htdocs/noh2", with_ssl=True)
        self.add_line(f"""
              Protocols http/1.1
            SSLCertificateKeyFile conf/ssl/cert.pkey
            SSLCertificateFile conf/ssl/noh2.{self.env.http_tld}_cert.pem
            SSLOptions +StdEnvVars""")
        self.end_vhost()
        self.start_vhost(self.env.http_port, "noh2", aliases=["noh2-alias"], doc_root="htdocs/noh2", with_ssl=False)
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
