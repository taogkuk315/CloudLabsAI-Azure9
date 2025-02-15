# Copyright 2015 greenbytes GmbH (https://www.greenbytes.de)
#
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

lib_LTLIBRARIES    = mod_h2test.la

mod_h2test_la_CPPFLAGS = -std=c99 -D_GNU_SOURCE -Werror @WERROR_CFLAGS@
mod_h2test_la_LDFLAGS = -module -avoid-version -export-symbols-regex h2test_module

OBJECTS = \
    mod_h2test.c

HFILES = \
    mod_h2test.h

mod_h2test_la_SOURCES = $(HFILES) $(OBJECTS)

all: mod_h2test.la

install-libLTLIBRARIES:
	@: # override

install-exec-local: mod_h2test.la
	$(MKDIR_P) $(DESTDIR)/@LIBEXEC_DIR@
	$(APXS) -i -S LIBEXECDIR=$(DESTDIR)/@LIBEXEC_DIR@ -n h2 mod_h2test.la


