
# mod_h[ttp]2 - http/2 for Apache httpd

Copyright 2015-2018 greenbytes GmbH

This repository contains `mod_h[ttp]2` and `mod_proxy_h[ttp]2` from Apache httpd as a standalone build. It servers as early access to features and fixes before being shipped in the next Apache release. It has **alpha status** and features might be removed again.

## Status

**`mod_h[ttp]2` is an official Apache httpd module**, first released in 2.4.17. See [Apache downloads](https://httpd.apache.org/download.cgi) to get a released version. `mod_proxy_h[ttp]2` has been released in 2.4.23.

What you find here are **early experience versions** for people who like living on the edge and want to help me test not yet released changes.

If you want HTTP/2 in your production environment, please head over to the official releases at Apache and grab one of those or wait until the various OS distributions have assembled one for you. 

## Current Version

The versions here are _based_ on the lastest Apache httpd 2.4.x release. There is no guarantee that these will be released unchanged by Apache. But you are welcome to test it and give feedback.

## Install

You need a built Apache httpd 2.4.34, including apxs and headers to compile and 
run this module. Additionally, you need an installed libnghttp2, at least in version
1.7.0. And additionally, you want an installed OpenSSL 1.0.2 or later.

tl;dr

**You need an installed recent Apache 2.4.x**

If you do not have that or don't know how to get it, look at google, stackoverflow, Apache mailing lists or your Linux distro. Not here!

## Apache 2.4.x Packages

* **Ubuntu**: [ppa by ondrej](https://launchpad.net/~ondrej/+archive/ubuntu/apache2) for Ubuntu 14.04 and others
* **Fedora**: [shipped in Fedora 23 and later](https://bodhi.fedoraproject.org/updates/?packages=httpd)
* **Debian** sid (unstable) [how to install debian sid](https://wiki.debian.org/InstallFAQ#Q._How_do_I_install_.22unstable.22_.28.22sid.22.29.3F)
* **Gentoo**: [latest stable](https://packages.gentoo.org/packages/www-servers/apache)
* **FreeBSD**: [Apache 2.4 port includes mod_http2](http://www.freshports.org/www/apache24/) / [mod_http2-devel port in review](https://reviews.freebsd.org/D5220)

## Changes

See ```ChangeLog``` for details.

## Tests

I decided to make the test suite part of this repository again. The existing suite resides
in test Apache httpd test repository and is a set of shell scripts. It works, but I miss
features that professional test frameworks bring. The tests included here use ```pytest``` which I think is an excellent way to do tests. I use it also in my Let's Encrypt module ```mod_md```. 

You can build the module without having the test prerequisites. If you want to run them, however, you need ```pytest```, ```python``` and a ```curl``` with http2 support. Then you can

```
> make
> make test
```


## `mod_proxy_http2`

(Disclaimer: the HTTP/2 proxy module is experimental. It can not be considered production ready.)

This module is part of the Apache httpd proxy architecture and functions similar to `mod_proxy_http` 
and friends. To configure it, you need to use ```h2:``` or ```h2c:``` in the proxy URL. Example:
```
<Proxy "balancer://h2-local">
    BalancerMember "h2://test.example.org:SUBST_PORT_HTTPS_SUBST"
</Proxy>
<Proxy "balancer://h2c-local">
    BalancerMember "h2c://test.example.org:SUBST_PORT_HTTP_SUBST"
</Proxy>

<IfModule proxy_http2_module>
    ProxyPass "/h2proxy" "balancer://h2-local"
    ProxyPassReverse "/h2proxy" "balancer://h2-local"
    ProxyPass "/h2cproxy" "balancer://h2c-local"
    ProxyPassReverse "/h2cproxy" "balancer://h2c-local"
</IfModule>
```
This will only work under the following conditions:
* the backend speaks HTTP/2, the module will not fallback to HTTP/1.1
* the backend supports HTTP/2 direct mode (see also ```H2Direct``` directive of ```mod_http2```)

All other common httpd ```proxy``` directives also apply.

What it will ***not*** do and what is ***untested***:
* fallback to HTTP/1.1
* be very smart when the number of concurrent streams in the backend differs from the local settings
* load balance between open connections dynamically
* forward any HTTP/2 priority information
* support HTTP/2 PUSH from the backend

What it ***will*** do:
* work with frontend HTTP/1.1 connections
* reuse open HTTP/2 connections from the balancer
* with a frontent HTTP/2 connection, all streams against the same backend will be handled in a single thread.


## Documentation

There is the official [Apache documentation](https://httpd.apache.org/docs/2.4/en/mod/mod_http2.html) of the module, which you will not find here.

I also compiled a [how to h2 in apache](https://icing.github.io/mod_h2/howto.html) document with advice on how to deploy, configure and verify your ```mod_h[ttp]2``` installation.

## Build from git

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

## Licensing

Please see the file called LICENSE.


## Credits

This work has been funded by the GSM Association (http://gsma.com). The module
itself was heavily influenced by mod_spdy, the Google implementation of their
SPDY protocol. And without Tatsuhiro Tsujikawa excellent nghttp2 work, this
would not have been possible.


Münster, 19.04.2016,

Stefan Eissing, greenbytes GmbH

Copying and distribution of this file, with or without modification,
are permitted in any medium without royalty provided the copyright
notice and this notice are preserved.  This file is offered as-is,
without warranty of any kind. See LICENSE for details.


