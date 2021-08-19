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


HTTP_PORT = 12345
HTTPS_PORT = 12346

GEN          = gen
INST_DIR     = gen/install
BLD_PREFIX   = $(PWD)/gen/install

OS           = $(shell uname -s)

SUB_DIRS     = nghttp2 httpd test mod_h2

CURL         = $(INST_DIR)/bin/curl


.PHONY: all test httpd nghttp2 start stop clean distclean mod_h2

all: httpd mod_h2

clean:
	@rm -rf $(GEN)
	@make -C test clean

distclean:
	@rm -rf $(GEN)
	$(foreach sd, $(SUB_DIRS), make -C $(sd) distclean; )

start: $(INST_DIR)/.test-setup
	@$(INST_DIR)/bin/apachectl restart

stop:
	@$(INST_DIR)/bin/apachectl stop

test: httpd mod_h2
	make -C test test

loadtest: httpd mod_h2
	make -C test loadtest

nghttp2: httpd
	make -C nghttp2

httpd:
	make -C httpd

$(INST_DIR)/.mod_h2-installed: \
        mod_h2/Makefile \
        $(wildcard mod_h2/*.c) \
        $(wildcard mod_h2/*.h)
	make -C mod_h2 install
	@touch $(INST_DIR)/.mod_h2-installed

mod_h2: \
    $(INST_DIR)/.mod_h2-installed


################################################################################
# auto-thingies
#
mod_h2/Makefile: \
		$(INST_DIR)/.httpd-installed
	cd mod_h2 && autoreconf -i
	cd mod_h2 && ./configure --with-apxs=../$(INST_DIR)/bin/apxs

################################################################################
# Install the local httpd for our tests
#
$(INST_DIR)/.httpd-installed: \
		$(INST_DIR)/.nghttp2-installed
	make -C httpd install

################################################################################
# Install the local nghttp2
#
$(INST_DIR)/.nghttp2-installed:
	make -C nghttp2 install

