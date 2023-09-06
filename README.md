
#mod_h[ttp]2 - http/2 for Apache httpd

Copyright (C) 2015 greenbytes GmbH

This repository contains the `mod_h[http]2` for Apache httpd. It enables the HTTP2
protocol inside the server, using nghttp2 (https://nghttp2.org) as base engine.

##Status
**Officially an Apache httpd module**, first released in 2.4.17. See [Apache downloads](https://httpd.apache.org/download.cgi) to get a released version.

What you find here are **early experience versions** for people who like living on the edge and want to help me test not yet released changes.

If you want HTTP/2 in your production environment, please head over to the official releases at Apache and grab one of those or wait until the various OS distributions have assembled one for you.

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
> ./configure
> make
```


##Licensing
Please see the file called LICENSE.


##Credits
This work has been funded by the GSM Association (http://gsma.com). The module
itself was heavily influenced by mod_spdy, the Google implementation of their
SPDY protocol. And without Tatsuhiro Tsujikawa excellent nghttp2 work, this
would not have been possible.


Münster, 09.10.2015,

Stefan Eissing, greenbytes GmbH

Copying and distribution of this file, with or without modification,
are permitted in any medium without royalty provided the copyright
notice and this notice are preserved.  This file is offered as-is,
without warranty of any kind. See LICENSE for details.


