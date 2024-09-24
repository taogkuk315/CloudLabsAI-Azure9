#
# mod-h2 test suite
# check GET variations
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
    HttpdConf().add_vhost_cgi().add_vhost_test1().install()
    assert TestEnv.apache_restart() == 0
        
def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0

class TestStore:

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)
    
    # check SSL environment variables from CGI script
    def test_003_01(self):
        url = TestEnv.mkurl("https", "cgi", "/hello.py")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/2.0" == r["response"]["json"]["protocol"]
        assert "on" == r["response"]["json"]["https"]
        assert "TLSv1.2" == r["response"]["json"]["ssl_protocol"]
        assert "on" == r["response"]["json"]["h2"]
        assert "off" == r["response"]["json"]["h2push"]

        r = TestEnv.curl_get(url, 5, [ "--http1.1" ])
        assert 200 == r["response"]["status"]
        assert "HTTP/1.1" == r["response"]["json"]["protocol"]
        assert "on" == r["response"]["json"]["https"]
        assert "TLSv1.2" == r["response"]["json"]["ssl_protocol"]
        assert "" == r["response"]["json"]["h2"]
        assert "" == r["response"]["json"]["h2push"]

    # retrieve a html file from the server and compare it to its source
    def test_003_02(self):
        with open(TestEnv.e2e_src( "htdocs/test1/index.html"), mode='rb') as file:
            src = file.read()

        url = TestEnv.mkurl("https", "test1", "/index.html")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        assert src == r["response"]["body"]

        url = TestEnv.mkurl("https", "test1", "/index.html")
        r = TestEnv.curl_get(url, 5, [ "--http1.1" ])
        assert 200 == r["response"]["status"]
        assert "HTTP/1.1" == r["response"]["protocol"]
        assert src == r["response"]["body"]

    # retrieve chunked content from a cgi script
    def check_necho(self, n, text):
        url = TestEnv.mkurl("https", "cgi", "/necho.py")
        r = TestEnv.curl_get(url, 5, [ "-F", ("count=%d" % (n)), "-F", ("text=%s" % (text)) ])
        assert 200 == r["response"]["status"]
        exp = ""
        for i in range(n):
            exp += text + "\n"
        assert exp == r["response"]["body"]
    
    def test_003_10(self):
        self.check_necho(10, "0123456789")

    def test_003_11(self):
        self.check_necho(100, "0123456789")

    def test_003_12(self):
        self.check_necho(1000, "0123456789")

    def test_003_13(self):
        self.check_necho(10000, "0123456789")

    def test_003_14(self):
        self.check_necho(100000, "0123456789")

    # github issue #126
    def test_003_20(self):
        url = TestEnv.mkurl("https", "test1", "/006/")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert '''<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN">
<html>
 <head>
  <title>Index of /006</title>
 </head>
 <body>
<title>My Header Title</title>
<ul><li><a href="/"> Parent Directory</a></li>
<li><a href="006.css"> 006.css</a></li>
<li><a href="006.js"> 006.js</a></li>
<li><a href="header.html"> header.html</a></li>
</ul>
</body></html>
''' == r["response"]["body"]

    # github issue #133
    def clean_header(self, s):
        s = re.sub(r'\r\n', '\n', s, flags=re.MULTILINE)
        s = re.sub(r'^date:.*\n', '', s, flags=re.MULTILINE)
        s = re.sub(r'^server:.*\n', '', s, flags=re.MULTILINE)
        s = re.sub(r'^last-modified:.*\n', '', s, flags=re.MULTILINE)
        s = re.sub(r'^etag:.*\n', '', s, flags=re.MULTILINE)
        return re.sub(r'^accept-ranges:.*\n', '', s, flags=re.MULTILINE)
        
    def test_003_21(self):
        url = TestEnv.mkurl("https", "test1", "/index.html")
        r = TestEnv.curl_get(url, 5, [ "-I" ])
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        s = self.clean_header(r["response"]["body"])
        assert '''HTTP/2 200 
content-length: 2007
content-type: text/html

''' == s

        r = TestEnv.curl_get(url, 5, [ "-I", url ])
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        s = self.clean_header(r["response"]["body"])
        assert '''HTTP/2 200 
content-length: 2007
content-type: text/html

HTTP/2 200 
content-length: 2007
content-type: text/html

''' == s

    # test conditionals: if-modified-since
    def test_003_30(self):
        url = TestEnv.mkurl("https", "test1", "/index.html")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        h = r["response"]["header"]
        assert "last-modified" in h
        lastmod = h["last-modified"]
        r = TestEnv.curl_get(url, 5, [ '-H', ("if-modified-since: %s" % lastmod) ])
        assert 304 == r["response"]["status"]

    # test conditionals: if-etag
    def test_003_31(self):
        url = TestEnv.mkurl("https", "test1", "/index.html")
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        h = r["response"]["header"]
        assert "etag" in h
        etag = h["etag"]
        r = TestEnv.curl_get(url, 5, [ '-H', ("if-none-match: %s" % etag) ])
        assert 304 == r["response"]["status"]


    # test various response body lengths to work correctly 
    def test_003_40(self):
        n = 1001
        while n <= 1025024:
            url = TestEnv.mkurl("https", "cgi", "/mnot164.py?count=%d&text=X" % (n))
            r = TestEnv.curl_get(url, 5)
            assert 200 == r["response"]["status"]
            assert "HTTP/2" == r["response"]["protocol"]
            assert n == len(r["response"]["body"])
            n *= 2

    # test various response body lengths to work correctly 
    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    @pytest.mark.parametrize("n", [
        0, 1, 1291, 1292, 80000, 80123, 81087, 98452
    ])
    def test_003_41(self, n):
        url = TestEnv.mkurl("https", "cgi", "/mnot164.py?count=%d&text=X" % (n))
        r = TestEnv.curl_get(url, 5)
        assert 200 == r["response"]["status"]
        assert "HTTP/2" == r["response"]["protocol"]
        assert n == len(r["response"]["body"])
        
