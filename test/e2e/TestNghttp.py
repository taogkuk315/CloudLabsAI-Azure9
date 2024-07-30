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
                    "promises" : []
            }
        return streams[sid] if sid in streams else None

    def _raw( self, url, timeout, options ) :
        u = urlparse(url)
        args = [ self.NGHTTP, "-v" ]
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
        r = self._run( args )
        if 0 == r["rv"]:
            # getting meta data and response body out of nghttp's output
            # is a bit tricky. Without '-v' we just get the body. With '-v' meta
            # data and timings in both directions are listed. 
            # We rely on response :status: to be unique and on 
            # response body not starting with space.
            # Something not good enough for general purpose, but for these tests.
            body = ""
            stream = 0
            streams = {}
            skip_indents = True
            lines = re.findall(r'[^\n]*\n', r["out"]["text"], re.MULTILINE)
            print "%d lines:" % len(lines)
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
            
            r["streams"] = streams
            
            if main_stream in streams:
                r["response"] = streams[main_stream]["response"]
        return r

    def get( self, url, timeout=5, options=None ) :
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
        fname = os.path.basename(fpath)
        if not options:
            options = []
        options.extend([ "--data=%s" % fpath ])
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



