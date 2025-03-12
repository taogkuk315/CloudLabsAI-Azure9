
# mod_h[ttp]2 - http/2 for Apache httpd

This repository contains `mod_h[ttp]2` and `mod_proxy_h[ttp]2` from Apache httpd as a standalone build. It servers as early access to features and fixes before being shipped in the next Apache release. Both modules can be considered **production ready** and stable as shipped by the Apache project.

## Status

**`mod_h[ttp]2` is an official Apache httpd module** since release 2.4.17. `mod_proxy_h[ttp]2` has been added in Apache in 2.4.23. The versions here at github are for more frequent releases than the Apache schedule provides for.

## Thanks

The following beautiful people have directly contributed to this project via commits over the years: 
Julian Reschke, Lubos Uhliarik, Luca Toscano, MATSUMOTO Ryosuke,
 Michael Kaufmann, Michael Köller, Mike Frysinger, Nicholas Race,
 Nicolas Noble, Petri Koistinen, Sam Hurst, Tatsuhiro Tsujikawa.
 
## Install

You need a built Apache httpd 2.4.34 or newer, including apxs and headers to compile and 
run this module. Additionally, you need an installed libnghttp2, at least in version
1.7.0. And additionally, you want an installed OpenSSL 1.0.2 or later.

tl;dr

**You need an installed recent Apache 2.4.x**

## Apache 2.4.x Packages

* **Ubuntu**: [ppa by ondrej](https://launchpad.net/~ondrej/+archive/ubuntu/apache2) for Ubuntu 14.04 and others
* **Fedora**: [shipped in Fedora 23 and later](https://bodhi.fedoraproject.org/updates/?packages=httpd)
* **Debian** sid (unstable) [how to install debian sid](https://wiki.debian.org/InstallFAQ#Q._How_do_I_install_.22unstable.22_.28.22sid.22.29.3F)
* **Gentoo**: [latest stable](https://packages.gentoo.org/packages/www-servers/apache)
* **FreeBSD**: [Apache 2.4 port includes mod_http2](http://www.freshports.org/www/apache24/) / [mod_http2 port](http://www.freshports.org/www/mod_http2/)

## Changes

See ```ChangeLog``` for details.

## Tests

I decided to make the test suite part of this repository again. The existing suite resides
in test Apache httpd test repository and is a set of shell scripts. It works, but I miss
features that professional test frameworks bring. The tests included here use ```python3``` and ```pytest``` which I think is an excellent way to do tests. I use it also in my Let's Encrypt module ```mod_md```. 

You can build the module without having the test prerequisites. If you want to run them, however, you need ```pytest```, ```python3``` and a ```curl``` with http2 support. Then you can

```
> make
> make test
```


## `mod_proxy_http2`

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


## Documentation

The official [Apache documentation of the module](https://httpd.apache.org/docs/2.4/en/mod/mod_http2.html).

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


Münster, 04.11.2019,

Stefan Eissing, greenbytes GmbH

Copying and distribution of this file, with or without modification,
are permitted in any medium without royalty provided the copyright
notice and this notice are preserved.  This file is offered as-is,
without warranty of any kind. See LICENSE for details.


