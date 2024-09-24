#!/usr/bin/env python

import cgi
import cgitb; cgitb.enable()
import os
import sys

sys.stdout = os.fdopen(sys.stdout.fileno(), 'w', 0)

try:
    form = cgi.FieldStorage()
    count = form['count'].value
    text = form['text'].value
except KeyError:
    text="a"
    count=77784
    
    
print "Status: 200 OK"
print "Content-Type: text/html"
print
sys.stdout.write(text*int(count))

