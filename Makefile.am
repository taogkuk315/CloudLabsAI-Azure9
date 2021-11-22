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
SUBDIRS         = httpd nghttp2 mod_h2
DIST_SUBDIRS    = $(SUBDIRS) test

ACLOCAL_AMFLAGS = -I m4


dist_doc_DATA   = README


.PHONY: test loadtest start stop restart

start:
	make -C test start

restart:
	make -C test restart

stop:
	make -C test stop

test:
	make -C test test

loadtest:
	make -C test loadtest

