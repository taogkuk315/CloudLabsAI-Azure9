###################################################################################################
# Utility class for calling and analysing nghttp calls
#
# (c) 2019 greenbytes GmbH, Stefan Eissing
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
from shutil import copyfile
from urlparse import urlparse

def _get_path(x):
    return x["path"]
    
class Nghttp:

    def __init__( self, path, connect_addr=None, tmp_dir="/tmp" ) :
        self.NGHTTP = path
        self.CONNECT_ADDR = connect_addr
        self.TMP_DIR = tmp_dir

    def get_stream( cls, streams, sid ) :
        sid = int(sid)
        if not sid in streams:
            streams[sid] = {
                    "id" : sid,
                    "header" : {},
                    "request" : {
                        "id" : sid,
                        "body" : "" 
                    },
                    "response" : {
                        "id" : sid, 
                        "body" : "" 
                    },
                    "paddings" : [],
                    "promises" : []
            }
        return streams[sid] if sid in streams else None

    def _baserun( self, url, timeout, options ) :
        u = urlparse(url)
        args = [ self.NGHTTP ]
        if self.CONNECT_ADDR:
            connect_host = self.CONNECT_ADDR
            args.append("--header=host: %s:%s" % (u.hostname, u.port))
        else:
            connect_host = u.hostname
        
        if options:
            args.extend(options)
        nurl = "%s://%s:%s/%s" % (u.scheme, connect_host, u.port, u.path)
        if u.query:
            nurl = "%s?%s" % (nurl, u.query)
        args.append( nurl )
        return self._run( args )
    
    def parse_output( self, text ):
        # getting meta data and response body out of nghttp's output
        # is a bit tricky. Without '-v' we just get the body. With '-v' meta
        # data and timings in both directions are listed. 
        # We rely on response :status: to be unique and on 
        # response body not starting with space.
        # Something not good enough for general purpose, but for these tests.
        output = {}
        body = ""
        stream = 0
        streams = {}
        skip_indents = True
        lines = re.findall(r'[^\n]*\n', text, re.MULTILINE)
        for lidx, l in enumerate(lines):
            m = re.match(r'\[.*\] recv \(stream_id=(\d+)\) (\S+): (\S*)', l)
            if m:
                s = self.get_stream( streams, m.group(1) )
                hname = m.group(2)
                hval = m.group(3)
                print ("stream %d header %s: %s" % (s["id"], hname, hval))
                header = s["header"]
                if hname in header: 
                    header[hname] += ", %s" % hval
                else:
                    header[hname] = hval
                body = ""
                continue

            m = re.match(r'\[.*\] recv HEADERS frame <.* stream_id=(\d+)>', l)
            if m:
                s = self.get_stream( streams, m.group(1) )
                if s:
                    print "stream %d: recv %d header" % (s["id"], len(s["header"])) 
                    response = s["response"]
                    if "header" in response:
                        prev = {
                            "header" : response["header"]
                        }
                        if "previous" in response:
                            prev["previous"] = response["previous"]
                        response["previous"] = prev
                    response["header"] = s["header"]
                    s["header"] = {} 
                body = ""
                continue
            
            m = re.match(r'(.*)\[.*\] recv DATA frame <length=(\d+), .*stream_id=(\d+)>', l)
            if m:
                s = self.get_stream( streams, m.group(3) )
                body += m.group(1)
                blen = int(m.group(2))
                if s:
                    print "stream %d: %d DATA bytes added" % (s["id"], blen)
                    padlen = 0
                    if len(lines) > lidx + 2:
                        mpad = re.match(r' +\(padlen=(\d+)\)', lines[lidx+2])
                        if mpad: 
                            padlen = int(mpad.group(1))
                    s["paddings"].append(padlen)
                    blen -= padlen
                    s["response"]["body"] += body[-blen:]
                body = ""
                skip_indents = True
                continue
                
            m = re.match(r'\[.*\] recv PUSH_PROMISE frame <.* stream_id=(\d+)>', l)
            if m:
                s = self.get_stream( streams, m.group(1) )
                if s:
                    # headers we have are request headers for the PUSHed stream
                    # these have been received on the originating stream, the promised
                    # stream id it mentioned in the following lines
                    print "stream %d: %d PUSH_PROMISE header" % (s["id"], len(s["header"]))
                    if len(lines) > lidx+2:
                        m2 = re.match(r'\s+\(.*promised_stream_id=(\d+)\)', lines[lidx+2])
                        if m2:
                            s2 = self.get_stream( streams, m2.group(1) )
                            s2["request"]["header"] = s["header"]
                            s["promises"].append(s2)
                    s["header"] = {} 
                continue
                    
            m = re.match(r'(.*)\[[ \d\.]+\] recv (\S+) frame <length=(\d+), .*stream_id=0>', l)
            if m:
                print "recv frame %s on stream 0" % m.group(2)
                body += m.group(1)
                skip_indents = True
                continue
                
            if skip_indents and l.startswith('      '):
                continue
            if "[" != l[0]:
                skip_indents = None
                body += l

            m = re.match(r'(.*)\[[ \d\.]+\] send (\S+) frame <length=(\d+), .*stream_id=0>', l)
            if m:
                print "send frame %s on stream 0" % m.group(2)
                body += m.group(1)
                skip_indents = True
                continue
                
            if skip_indents and l.startswith('      '):
                continue
            if "[" != l[0]:
                skip_indents = None
                body += l
                
        # the main request is done on the lowest odd numbered id
        main_stream = 99999999999
        for sid in streams:
            s = streams[sid]
            s["response"]["status"] = int(s["response"]["header"][":status"])
            if (sid % 2) == 1 and sid < main_stream:
                main_stream = sid
        
        output["streams"] = streams
        if main_stream in streams:
            output["response"] = streams[main_stream]["response"]
            output["paddings"] = streams[main_stream]["paddings"]
        return output
    
    def _raw( self, url, timeout, options ) :
        args = [ "-v" ]
        if options:
            args.extend(options)
        r = self._baserun( url, timeout, args )
        if 0 == r["rv"]:
            o = self.parse_output(r["out"]["text"])
            for name in o:
                r[name] = o[name] 
        return r

    def get( self, url, timeout=5, options=None ) :
        return self._raw( url, timeout, options )

    def assets( self, url, timeout=5, options=None ) :
        if not options:
            options = []
        options.extend([ "-ans" ]) 
        r = self._baserun( url, timeout, options )
        assets = []
        if 0 == r["rv"]:
            lines = re.findall(r'[^\n]*\n', r["out"]["text"], re.MULTILINE)
            for lidx, l in enumerate(lines):
                m = re.match(r'\s*(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+/(.*)', l)
                if m:
                    assets.append({
                        "path" : m.group(7),
                        "status" : int(m.group(5)),
                        "size" : m.group(6)
                    })
        assets.sort(key=_get_path)
        r["assets"] = assets
        return r

    def post_data( self, url, data, timeout=5, options=None ) :
        reqbody = ("%s/nghttp.req.body" % self.TMP_DIR)
        with open(reqbody, 'wb') as f:
            f.write(data)
        if not options:
            options = []
        options.extend([ "--data=%s" % reqbody ])
        return self._raw( url, timeout, options )

    def post_name( self, url, name, timeout=5, options=None ) :
        reqbody = ("%s/nghttp.req.body" % self.TMP_DIR)
        with open(reqbody, 'w') as f:
            f.write("--DSAJKcd9876\n")
            f.write("Content-Disposition: form-data; name=\"value\"; filename=\"xxxxx\"\n")
            f.write("Content-Type: text/plain\n")
            f.write("\n%s\n" % name)
            f.write("--DSAJKcd9876\n")
        if not options:
            options = []
        options.extend([ "--data=%s" % reqbody ])
        return self._raw( url, timeout, options )

    def upload( self, url, fpath, timeout=5, options=None ) :
        if not options:
            options = []
        options.extend([ "--data=%s" % fpath ])
        return self._raw( url, timeout, options )

    def upload_file( self, url, fpath, timeout=5, options=None ) :
        fname = os.path.basename(fpath)
        reqbody = ("%s/nghttp.req.body" % self.TMP_DIR)
        with open(fpath, 'rb') as fin:
            with open(reqbody, 'w') as f:
                f.write("--DSAJKcd9876\n")
                f.write("Content-Disposition: form-data; name=\"xxx\"; filename=\"xxxxx\"\n")
                f.write("Content-Type: text/plain\n")
                f.write("\n")
                f.write("testing mod_h2\n")
                f.write("--DSAJKcd9876\n")
                f.write("Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\n" % (fname))
                f.write("Content-Type: application/octet-stream\n")
                f.write("Content-Transfer-Encoding: binary\n")
                f.write("\n")
                f.write(fin.read())
                f.write("\n")
                f.write("--DSAJKcd9876\n")
        if not options:
            options = []
        options.extend([ "--data=%s" % reqbody, 
            "--expect-continue", 
            "-HContent-Type: multipart/form-data; boundary=DSAJKcd9876" ])
        return self._raw( url, timeout, options )

    def _run( self, args, input=None ) :
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



