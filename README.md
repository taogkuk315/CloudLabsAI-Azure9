
#mod_h[ttp]2 - http/2 for Apache httpd

Copyright (C) 2015, 2016 greenbytes GmbH

This repository contains the `mod_h[ttp]2` from Apache httpd as a standalone build. 

##Status
**An official Apache httpd module**, first released in 2.4.17. See [Apache downloads](https://httpd.apache.org/download.cgi) to get a released version.

What you find here are **early experience versions** for people who like living on the edge and want to help me test not yet released changes.

If you want HTTP/2 in your production environment, please head over to the official releases at Apache and grab one of those or wait until the various OS distributions have assembled one for you. 

##Current Version
The version 1.4.6 is **exactly** the one released in Apache httpd 2-.4.20.

Later versions willl be an **early experience version**
and there is no guarantee that it will be released as it is here by Apache. But you are welcome to test it and give feedback.

##Install

You need a built Apache httpd 2.4.20, including apxs and headers to compile and 
run this module. Additionally, you need an installed libnghttp2, at least in version
1.3.0. And additionally, you want an installed OpenSSL 1.0.2.

tl;dr

**You need an installed Apache 2.4.20 which already runs ```mod_http2``` in it.**

If you do not have that or don't know how to get it, look at google, stackoverflow, Apache mailing lists or your Linux distro. Not here!

##Apache 2.4.x Packages

* **Ubuntu**: [ppa by ondrej](https://launchpad.net/~ondrej/+archive/ubuntu/apache2) for Ubuntu 14.04 and others
* **Fedora**: [Rawhide includes httpd 2.4.17](http://rpmfind.net/linux/rpm2html/search.php?query=httpd)
* **Debian** sid (unstable) includes httpd 2.4.17. See [how to install debian sid](https://wiki.debian.org/InstallFAQ#Q._How_do_I_install_.22unstable.22_.28.22sid.22.29.3F)
* **FreeBSD**: [Apache 2.4 port includes mod_http2](http://www.freshports.org/www/apache24/) / [mod_http2-devel port in review](https://reviews.freebsd.org/D5220)

##Changes

See ```ChangeLog``` for details.

##Documenation
There is the official [Apache documentation](https://httpd.apache.org/docs/2.4/en/mod/mod_http2.html) of the module, which you will not find here.

I also compiled a [how to h2 in apache](https://icing.github.io/mod_h2/howto.html) document with advice on how to deploy, configure and verify your ```mod_h[ttp]2``` installation.

##Build from git
Still not dissuaded? Ok, here are some hints to get you started.
Building from git is easy, but please be sure that at least autoconf 2.68 is
used:

```
> autoreconf -i
> automake
> autoconf
> ./configure --with-apxs=<path to apxs>
> make
```

##Licensing
Please see the file called LICENSE.


##Credits
This work has been funded by the GSM Association (http://gsma.com). The module
itself was heavily influenced by mod_spdy, the Google implementation of their
SPDY protocol. And without Tatsuhiro Tsujikawa excellent nghttp2 work, this
would not have been possible.


Münster, 18.04.2016,

Stefan Eissing, greenbytes GmbH

Copying and distribution of this file, with or without modification,
are permitted in any medium without royalty provided the copyright
notice and this notice are preserved.  This file is offered as-is,
without warranty of any kind. See LICENSE for details.


