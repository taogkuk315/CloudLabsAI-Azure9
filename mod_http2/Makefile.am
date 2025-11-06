# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
SUBDIRS =

ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign

lib_LTLIBRARIES    = mod_http2.la mod_proxy_http2.la

mod_http2_la_CPPFLAGS = -std=c99 -D_GNU_SOURCE -Werror @WERROR_CFLAGS@
mod_http2_la_LDFLAGS = -module -avoid-version -export-symbols-regex http2_module

mod_proxy_http2_la_CPPFLAGS = -std=c99 -D_GNU_SOURCE -Werror @WERROR_CFLAGS@
mod_proxy_http2_la_LDFLAGS = -module -avoid-version -export-symbols-regex proxy_http2_module

OBJECTS = \
    h2_bucket_beam.c \
    h2_bucket_eos.c \
    h2_c1.c \
    h2_c1_io.c \
    h2_c2.c \
    h2_c2_filter.c \
    h2_config.c \
    h2_conn_ctx.c \
    h2_headers.c \
    h2_mplx.c \
    h2_protocol.c \
    h2_push.c \
    h2_request.c \
    h2_session.c \
    h2_stream.c \
    h2_switch.c \
    h2_util.c \
    h2_workers.c \
    mod_http2.c

HFILES = \
    h2.h \
    h2_bucket_beam.h \
    h2_bucket_eos.h \
    h2_c1.h \
    h2_c1_io.h \
    h2_c2.h \
    h2_c2_filter.h \
    h2_config.h \
    h2_conn_ctx.h \
    h2_headers.h \
    h2_mplx.h \
    h2_private.h \
    h2_protocol.h \
    h2_push.h \
    h2_request.h \
    h2_session.h \
    h2_stream.h \
    h2_switch.h \
    h2_util.h \
    h2_version.h \
    h2_workers.h \
    mod_http2.h

PROXY_HFILES = \
    h2.h \
    h2_proxy_session.h \
    h2_proxy_util.h \
    mod_proxy_http2.h

PROXY_OBJECTS = \
    h2_proxy_session.c \
    h2_proxy_util.c \
    mod_proxy_http2.c

mod_http2_la_SOURCES = $(HFILES) $(OBJECTS)

mod_proxy_http2_la_SOURCES = $(PROXY_HFILES) $(PROXY_OBJECTS)

all: mod_http2.la \
    mod_proxy_http2.la

install-libLTLIBRARIES:
	@: # override

install-exec-local: mod_http2.la mod_proxy_http2.la
	$(MKDIR_P) $(DESTDIR)/@LIBEXEC_DIR@
	$(APXS) -i -S LIBEXECDIR=$(DESTDIR)/@LIBEXEC_DIR@ -n h2 mod_http2.la
	$(APXS) -i -S LIBEXECDIR=$(DESTDIR)/@LIBEXEC_DIR@ -n h2 mod_proxy_http2.la


