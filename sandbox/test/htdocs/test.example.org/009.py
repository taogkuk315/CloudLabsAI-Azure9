#!/usr/bin/env python
# -*- coding: utf-8 -*-
import cgi, sys, time
import cgitb; cgitb.enable()

print "Content-Type: text/html;charset=UTF-8"
print

print """\
	<!DOCTYPE html><html><head>
	<title>HTML/2.0 Test File: 008 (server time)</title></head>
	<body><h1>HTML/2.0 Test File: 008</h1>"""

for i in range(60):
	s = time.strftime("%Y-%m-%d %H:%M:%S")
	print "<div>", s, "</div>"
	sys.stdout.flush()
	time.sleep(1)

print "</body></html>"