#
# mod-h2 test suite
# check POST variations
#

import datetime
import email.parser
import json
import os
import re
import sys
import time
import pytest
import subprocess

from threading import Thread
from TestEnv import TestEnv
from TestHttpdConf import HttpdConf

def setup_module(module):
    print("setup_module: %s" % module.__name__)
    TestEnv.init()
    TestEnv.setup_data_1k_1m()
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

    # upload and GET again using curl, compare to original content
    def curl_upload_and_verify(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/upload.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)
        r = TestEnv.curl_upload(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300

        r2 = TestEnv.curl_get( r["response"]["header"]["location"])
        assert r2["rv"] == 0
        assert r2["response"]["status"] == 200 
        with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
            src = file.read()
        assert src == r2["response"]["body"]

    def test_004_01(self):
        self.curl_upload_and_verify( "data-1k", [ "--http1.1" ] )
        self.curl_upload_and_verify( "data-1k", [ "--http2" ] )

    def test_004_02(self):
        self.curl_upload_and_verify( "data-10k", [ "--http1.1" ] )
        self.curl_upload_and_verify( "data-10k", [ "--http2" ] )

    def test_004_03(self):
        self.curl_upload_and_verify( "data-100k", [ "--http1.1" ] )
        self.curl_upload_and_verify( "data-100k", [ "--http2" ] )

    def test_004_04(self):
        self.curl_upload_and_verify( "data-1m", [ "--http1.1" ] )
        self.curl_upload_and_verify( "data-1m", [ "--http2" ] )

    def test_004_05(self):
        self.curl_upload_and_verify( "data-1k", [ "-v", "--http1.1", "-H", "Expect: 100-continue" ] )
        self.curl_upload_and_verify( "data-1k", [ "-v", "--http2", "-H", "Expect: 100-continue" ] )

    @pytest.mark.skipif(True, reason="python3 regresses in chunked inputs to cgi")
    def test_004_06(self):
        self.curl_upload_and_verify( "data-1k", [ "--http1.1", "-H", "Content-Length: " ] )
        self.curl_upload_and_verify( "data-1k", [ "--http2", "-H", "Content-Length: " ] )

    @pytest.mark.parametrize("name, value", [
        ( "HTTP2", "on"),
        ( "H2PUSH", "off"),
        ( "H2_PUSHED", ""),
        ( "H2_PUSHED_ON", ""),
        ( "H2_STREAM_ID", "1"),
        ( "H2_STREAM_TAG", r'\d+-1'),
    ])
    def test_004_07(self, name, value):
        url = TestEnv.mkurl("https", "cgi", "/env.py")
        r = TestEnv.curl_post_value( url, "name", name )
        assert r["rv"] == 0
        assert r["response"]["status"] == 200
        m = re.match("{0}=(.*)".format(name), r["response"]["body"].decode('utf-8'))
        assert m
        assert re.match(value, m.group(1)) 

    # verify that we parse nghttp output correctly
    def check_nghttp_body(self, ref_input, nghttp_output):
        with open(TestEnv.e2e_src( os.path.join(TestEnv.GEN_DIR, ref_input) ), mode='rb') as f:
            refbody = f.read()
        with open(TestEnv.e2e_src( nghttp_output), mode='rb') as f:
            text = f.read()
        o = TestEnv.nghttp().parse_output(text)
        assert "response" in o
        assert "body" in o["response"]
        if refbody != o["response"]["body"]:
            with open(TestEnv.e2e_src( os.path.join(TestEnv.GEN_DIR, '%s.parsed' % ref_input) ), mode='bw') as f:
                f.write( o["response"]["body"] )
        assert len(refbody) == len(o["response"]["body"])
        assert refbody == o["response"]["body"]
    
    def test_004_20(self):
        self.check_nghttp_body( 'data-1k', 'data/nghttp-output-1k-1.txt') 
        self.check_nghttp_body( 'data-10k', 'data/nghttp-output-10k-1.txt') 
        self.check_nghttp_body( 'data-100k', 'data/nghttp-output-100k-1.txt') 


    # POST some data using nghttp and see it echo'ed properly back
    def nghttp_post_and_verify(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/echo.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)

        r = TestEnv.nghttp().upload(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300

        with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
            src = file.read()
        assert src == r["response"]["body"]

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_004_21(self):
        self.nghttp_post_and_verify( "data-1k", [ ] )
        self.nghttp_post_and_verify( "data-10k", [ ] )
        self.nghttp_post_and_verify( "data-100k", [ ] )
        self.nghttp_post_and_verify( "data-1m", [ ] )

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_004_22(self):
        self.nghttp_post_and_verify( "data-1k", [ "--no-content-length" ] )
        self.nghttp_post_and_verify( "data-10k", [ "--no-content-length" ] )
        self.nghttp_post_and_verify( "data-100k", [ "--no-content-length" ] )
        self.nghttp_post_and_verify( "data-1m", [ "--no-content-length" ] )


    # upload and GET again using nghttp, compare to original content
    def nghttp_upload_and_verify(self, fname, options=None):
        url = TestEnv.mkurl("https", "cgi", "/upload.py")
        fpath = os.path.join(TestEnv.GEN_DIR, fname)

        r = TestEnv.nghttp().upload_file(url, fpath, options=options)
        assert r["rv"] == 0
        assert r["response"]["status"] >= 200 and r["response"]["status"] < 300
        assert r["response"]["header"]["location"]

        r2 = TestEnv.nghttp().get(r["response"]["header"]["location"])
        assert r2["rv"] == 0
        assert r2["response"]["status"] == 200 
        with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
            src = file.read()
        assert src == r2["response"]["body"]

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_004_23(self):
        self.nghttp_upload_and_verify( "data-1k", [ ] )
        self.nghttp_upload_and_verify( "data-10k", [ ] )
        self.nghttp_upload_and_verify( "data-100k", [ ] )
        self.nghttp_upload_and_verify( "data-1m", [ ] )

    @pytest.mark.skipif(not TestEnv.has_nghttp(), reason="no nghttp command available")
    def test_004_24(self):
        self.nghttp_upload_and_verify( "data-1k", [ "--expect-continue" ] )
        self.nghttp_upload_and_verify( "data-100k", [ "--expect-continue" ] )

    @pytest.mark.skipif(True, reason="python3 regresses in chunked inputs to cgi")
    def test_004_25(self):
        self.nghttp_upload_and_verify( "data-1k", [ "--no-content-length" ] )
        self.nghttp_upload_and_verify( "data-10k", [  "--no-content-length" ] )
        self.nghttp_upload_and_verify( "data-100k", [  "--no-content-length" ] )
        self.nghttp_upload_and_verify( "data-1m", [  "--no-content-length" ] )

    def test_004_30(self):
        # issue: #203
        resource = "data-1k"
        full_length = 1000
        chunk = 200
        self.curl_upload_and_verify( resource, [ "-v", "--http2"] )
        logfile = os.path.join(TestEnv.HTTPD_LOGS_DIR, "test_004_30")
        if os.path.isfile(logfile):
            os.remove(logfile)
        HttpdConf().add_line("""
LogFormat "{ \\"request\\": \\"%r\\", \\"status\\": %>s, \\"bytes_resp_B\\": %B, \\"bytes_tx_O\\": %O, \\"bytes_rx_I\\": %I, \\"bytes_rx_tx_S\\": %S }" issue_203
CustomLog logs/test_004_30 issue_203
        """).add_vhost_cgi().install()
        assert TestEnv.apache_restart() == 0
        url = TestEnv.mkurl("https", "cgi", "/files/{0}".format(resource))
        r = TestEnv.curl_get(url, 5, ["--http2"])
        assert 200 == r["response"]["status"]
        r = TestEnv.curl_get(url, 5, ["--http1.1", "-H", "Range: bytes=0-{0}".format(chunk-1)])
        assert 206 == r["response"]["status"]
        assert chunk == len(r["response"]["body"].decode('utf-8'))
        r = TestEnv.curl_get(url, 5, ["--http2", "-H", "Range: bytes=0-{0}".format(chunk-1)])
        assert 206 == r["response"]["status"]
        assert chunk == len(r["response"]["body"].decode('utf-8'))
        # now check what response lengths have actually been reported
        lines = open(logfile).readlines()
        log_h2_full = json.loads(lines[-3])
        log_h1 = json.loads(lines[-2])
        log_h2 = json.loads(lines[-1])
        assert log_h2_full['bytes_rx_I'] > 0
        assert log_h2_full['bytes_resp_B'] == full_length
        assert log_h2_full['bytes_tx_O'] > full_length
        assert log_h1['bytes_rx_I'] > 0         # input bytes recieved
        assert log_h1['bytes_resp_B'] == chunk  # response bytes sent (payload)
        assert log_h1['bytes_tx_O'] > chunk     # output bytes sent
        assert log_h2['bytes_rx_I'] > 0
        assert log_h2['bytes_resp_B'] == chunk
        assert log_h2['bytes_tx_O'] > chunk
        
    def test_004_40(self):
        # echo content using h2test_module "echo" handler
        def post_and_verify(fname, options=None):
            url = TestEnv.mkurl("https", "cgi", "/h2test/echo")
            fpath = os.path.join(TestEnv.GEN_DIR, fname)
            r = TestEnv.curl_upload(url, fpath, options=options)
            assert r["rv"] == 0
            assert r["response"]["status"] >= 200 and r["response"]["status"] < 300
            
            ct = r["response"]["header"]["content-type"]
            mail_hd = "Content-Type: " + ct + "\r\nMIME-Version: 1.0\r\n\r\n"
            mime_msg = mail_hd.encode() + r["response"]["body"]
            # this MIME API is from hell
            body = email.parser.BytesParser().parsebytes(mime_msg)
            assert body
            assert body.is_multipart()
            filepart = None
            for part in body.walk():
                if fname == part.get_filename():
                    filepart = part
            assert filepart
            with open(TestEnv.e2e_src( fpath ), mode='rb') as file:
                src = file.read()
            assert src == filepart.get_payload(decode=True)
        
        post_and_verify( "data-1k", [ ] )

