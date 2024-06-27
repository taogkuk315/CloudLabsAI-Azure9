#!/usr/bin/env python
import sys, cgi, os

status = '200 Ok'

content = ''
for line in sys.stdin:
    content += line
    
# Just echo what we get
print "Status: 200"
print """Content-Type: application/data\n"""
print content,

