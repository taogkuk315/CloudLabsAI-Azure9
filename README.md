
#mod_h2 - http/2 for Apache httpd

Copyright (C) 2015 greenbytes GmbH

Copying and distribution of this file, with or without modification,
are permitted in any medium without royalty provided the copyright
notice and this notice are preserved.  This file is offered as-is,
without warranty of any kind. See LICENSE for details.


This repository contains the `mod_h2` for Apache httpd. It enables the HTTP2
protocol inside the server, using nghttp2 (https://nghttp2.org) as base engine.

##Status
**EARLY EXPERIENCE VERSION**: `mod_h2` has been donated into the Apache `httpd` project and has become part of that product. This repository is merely a copy of the module itself, plus some gift wrappings to make life easy for people who want to test drive it.

**WHY?** The module can already be downloaded and built as part of the Apache  from [httpd trunk (2.5-DEV)](http://httpd.apache.org/docs/trunk/). So why duplicate it here? The module will become part of the **2.4.x** branch, hopefully very soon, and needs testing on that branch. This configuration is not readily available somewhere else.

**EARLIER VERSIONS**: if you have an earlier version of `mod_h2` installed, please see the chapter [Migration](#Migration) for what you need to do.

##Features
This module supports the protocols "h2" (HTTP2 over TLS) and "h2c" (HTTP2 over plain HTTP connections via Upgrade). You can enable it for the whole server or
for specific virtual hosts only. More on this below on "Configuration".

Specifically, the protocols "h2" and its "h2c" cousins are supported. Also the `direct` mode for cleartext connection is enabled by default.

##Tested Platforms
**You can no longer just drop mod_h2 into a released httpd! See [Sandbox Installation](#Sandbox Installation).** 
* OS: Ubuntu 14.04, OS X 10.10
* Apache httpd 2.4.16 (patch needed)
* Openssl 1.0.1 + 1.0.2

##Sandbox

##Building
If you have a sandbox built from pre-0.9.x, you want to do a `make clean` before building the new version. At least throw away the header files in `sandbox/install/include/http*` and the httpd built in `sandbox/httpd/gen/httpd*`.

##Configuration
The test setup in test/conf/* that gets installed in gen/install for the
local httpd build contains some simple examples of how this module can
be configured.

There are several configuration commands available when mod_h2 is loaded,
such as:

* H2MaxSessionStreams n      maximum number of open streams per session, default: 100
* H2InitialWindowSize n      initial window size on client DATA, default: 16k
* H2MinWorkers n             minimum number of worker threads per child, default: mpm configured MaxWorkers/2
* H2MaxWorkers n             maximum number of worker threads per child, default: mpm configured thread limit/2
* H2StreamMaxMemSize n       maximum number of bytes buffered in memory for a stream, default: 64k
* H2AltSvc name=host:port    Announce an "alternate service" to clients (see https://http2.github.io/http2-spec/alt-svc.html for details), default: empty
* H2AltSvcMaxAge n           number of seconds Alt-Svc information is valid, default: will not be sent, specification defaults to 24h
* H2SerializeHeaders (on/off), "off"   serialize/parse request+response headers for streams, as if they arrived in HTTP/1 format. When off, certain parts of httpd core filters are disabled/replaced to allow for a more efficient handling. 
* H2Direct (on/off), "on"    to enable h2c direct mode on a non-TLS host, default: off
* H2SessionExtraFiles n      number of extra file handles a session might keep open to improve performance, depends on mpm module used and ulimit of processes, defaults to 5

All these configuration parameters can be set on servers/virtual hosts and
are not available on directory level. Note that Worker configuration is
only relevant on the base apache server and will be read - but ignored -
on any virtual hosts.


##Dependencies
1. The module is written in plain C and links to libnghttp2 (version 1.0.0 and up) 
and the Apache runtime. It needs a patched httpd 2.4. The patch is supplied in `sandbox/httpd/patches/core_protocols_release.patch` and is automatically applied in the `sandbox` build.
1. For ALPN/NPN protocol negotiation (the basic TLS HELLO part) to work,
at least a current OpenSSL 1.0.1 is needed.


##Installation
**You can no longer just drop mod_h2 into a released httpd! See [Sandbox Installation](#Sandbox Installation).** 

mod_h2 is using autoconf/automake for configuration and build handling. If you
have a git checkout, refer to 'Build from git' below. If you have a release
extracted, you need to:

```
> cd mod_h2-x.x.x
> ./configure
> make
```

This however only works for an already patched Apache httpd. If you are not comfortable with coding and patching, please use the sandbox installation.

For general handling of 'configure', see INSTALL. For mod_h2 specifically,
there are two arguments to know:
* `--enable-sandbox`     build a complete sandbox installation with own httpd, own libnghttp2
* `--enable-werror`      build with tons of compiler diagnostics enabled
* `--with-apxs=<pathtoapxs>` for a non-sandboxed installation where the apxs (from the apache development environment) is in an unusual location.

If you run 'configure' without arguments, it assumes a non-sandbox'ed built
where apxs and libnghttp2 are properly installed.


<a name="Sandbox Installation"></a>
###Sandbox Installation:
**You can no longer just drop mod_h2 into a released httpd!** 

This installation you configure with:

```
> cd mod_h2-x.x.x
> ./configure --enable-sandbox
> make
```

It will download sources for various libraries and the Apache httpd itself, depending on what is already on your system. It places everything in `./sandbox/install`: httpd, nghttp2, curl
and other fine things. For testing the module, or just trying it out, this
has the following advantages:
* conflicts with "wrong" versions already installed on your system are avoided
* you can do the installation and test without root privileges
* certain patches can be applied that are necessary for all features to work, see "Dependencies"."

<a name="Migration"></a>
##Migration
If you already have an installation of `mod_h2`, here is a list of things your might want to watch out for:

###Configuration
 * **From 0.8.x and earlier**: several configuration directives have changed. Specifically:
   * the directives `H2Engine`, `H2MaxHeaderListSize`, `H2HackMpmEvent`, `H2BufferOutput`, `H2BufferSize` and `H2BufferWriteMax` have disappeared. You will see errors when starting Apache and still have them. 
   * In order to enable/disable protocols for a server/vhost, the new `Protocols` directive will become part of core Apache. Here, you can specify the protocols that should be allowed for a server/vhost. If no such directive is given, all protocols are enabled.
   * The new directive `ProtocolsHonorOrder on|off` controls if the server should override client preferences in protocol selection. With `on` the order in which you configure the protocols determines the preference.

Due to a bug in Chrome (v44 at least), which gets the preference order wrong way around, the safe configuration for now is:

```
  Protocols h2 http/1.1
  ProtocolsHonorOrder on
```

###Server Support
From v0.9.x and onwards, `mod_h2` requires a `httpd` with support for the new `Protocols` directive. The server it builds in its sandbox is patched accordingly. If you want to load the module in your own server, you need to create one that
supports `Protocols`. A patch for a 2.4.16 httpd is supplied in `sandbox/httpd/patches/core_protocols_release.patch`.

###Client Support
Apache `httpd` will never ship a release with the TLS `NPN` feature enabled. NPN was experimental and is superceeded by `ALPN`. From 0.9.x onwards, this project in its sandbox, will also no longer support NPN. If you have clients remaining on the old one, you need to check with the maintainers of the client to get an updated version.
   
##Build from git
Building from git is easy, but please be sure that at least autoconf 2.68 is
used:

```
> autoreconf -i
> automake
> autoconf
> ./configure
> make
```

##Supported Platforms
mod_h2 has been developed under Ubuntu 14.04 LTS and OS X 10.10. The module
itself depends only on an installed APXS (the Apache runtime platform) and
libnghttp2. As long as both are present, the module itself should build
just fine.

Ubuntu :Install the prerequisite software. On a 14.04 LTS server, that should be:

```
> sudo apt-get install git gcc g++ libpcre3-dev libcunit1-dev libev-dev libjansson-dev libjemalloc-dev cython make binutils autoconf automake autotools-dev libtool pkg-config zlib1g-dev libssl-dev libxml2-dev libevent-dev python3.4-dev libevent-openssl-2.0-5 php5-cgi python-setuptools
```

OS X: on OS X 10.10, building the project requires a homebrew installation and the following packages installed via brew:
* pkg-config
* for httpd the Makefile will download and install:
    * pcre
    * apr + apr-util
    * openssl
  exact versions and download urls in httpd/Makefile
* for nghttp2 the Makefile will download and install:
    * zlib
    * libev
  exact versions and download urls in httpd/Makefile
* for sandbox tests you will need php5-cgi from homebrew

##Architecture, Limits, Details
See DISCUSS.


##Sandbox Testing
The sandbox build contains some test cases. In order to run those, you
need to:

```
> make
> make install
> make test
```

The sandbox creates its own httpd and nghttp2 installation in gen/install
and configures httpd to run on ports 12345+12346 (TLS). It also generates
a self-signed certificate for the servers under the name test.example.org
and test-ser.example.org.
You should make entries in /etc/hosts like

```
127.0.0.1       test.example.org        test
127.0.0.1       test-ser.example.org    test
```

for tests to work properly.

Another issue is testing with browsers like Chrome or Firefox. If you point
them at test.example.org, they will complain about the self-signed certificate,
offer you to connect anyway and, if you choose that, refuse to work. I think
they have a much stricter cert checking for HTTP/2 and the UI needs an update
here.

I myself configure an additional site into the sandbox server with a real
certificate and test browsers successfully there. But I cannot share this
certificate with the world. If there is a better way to test browser interop,
I am happy to be given pointers.

#Known Issues
* When multiple vhosts share the same certificate, browsers will reuse an open connection for all those requests. `mod_h2` currently only allows requests for the same host the connection was opened with. Some browsers throw then away the existing connection and open a new one. This can heavily impact performance. 
* If you test chrome/firefox against a httpd with mod_h2 and get "ERR_SPDY_INADEQUATE_TRANSPORT_SECURITY", this means that the browser considers the installed SSL certificates/chosen Ciphers as not good enough to use HTTP/2. The sandbox host test.example.org is properly configured so that you can confirm your intent and use the browsers. If you want to test your own setup, be aware that requirements are tighter than before.
* Some modules will not be fully compatible with HTTP/2 connections. mod_logio, for example, will not properly report the accumulated traffic per connection as requests are handled in sub-connecition and that data is never aggregated.

##Licensing
Please see the file called LICENSE.


##Credits
This work has been funded by the GSM Association (http://gsma.com). The module
itself was heavily influenced by mod_spdy, the Google implementation of their
SPDY protocol. And without Tatsuhiro Tsujikawa excellent nghttp2 work, this
would not have been possible.


Münster, 26.08.2015,

Stefan Eissing, greenbytes GmbH
