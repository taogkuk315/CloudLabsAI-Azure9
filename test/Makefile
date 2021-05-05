# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


HTTP_PORT = 12345
HTTPS_PORT = 12346

GEN          = gen
INST_DIR     = ../gen/install
BLD_PREFIX   = $(shell dirname $$PWD)/gen/install

OS           = $(shell uname -s)

CURL         = $(INST_DIR)/bin/curl


all: test

clean:
	@rm -rf $(GEN)

distclean:
	@rm -rf $(GEN)
	$(foreach sd, $(SUB_DIRS), make -C $(sd) distclean; )

################################################################################
# Test the local httpd
#
test: \
		$(INST_DIR)/.httpd-setup \
		$(INST_DIR)/.curl-installed
	@rm -rf $(GEN)/tmp
	@mkdir -p $(GEN)/tmp
	@$(INST_DIR)/bin/apachectl restart
	@echo -n checking index.html over http...
	@$(CURL) -sk --resolv test.local:$(HTTP_PORT):127.0.0.1 http://test.local:$(HTTP_PORT)/index.html > $(GEN)/tmp/http_index.html
	@diff  $(INST_DIR)/htdocs/index.html $(GEN)/tmp/http_index.html
	@echo ok.
	@echo -n checking index.html over https with HTTP/1...
	@$(CURL) -sk --http1.1 --resolv test.local:$(HTTPS_PORT):127.0.0.1 https://test.local:$(HTTPS_PORT)/index.html > $(GEN)/tmp/https_index.html
	@diff  $(INST_DIR)/htdocs/index.html $(GEN)/tmp/https_index.html
	@echo ok.
	@echo -n checking index.html over https with h2...
	@$(CURL) -sk --http2 --resolv test.local:$(HTTPS_PORT):127.0.0.1 https://test.local:$(HTTPS_PORT)/index.html > $(GEN)/tmp/h2_index.html || bash -c "echo -n failed as expected..."
	@echo ok.
	@$(INST_DIR)/bin/apachectl stop


################################################################################
# Setup the local httpd for our tests
#
SETUP_FILES = $(wildcard conf/*.conf) $(wildcard conf/sites/*.conf) \
    $(wildcard conf/mods/*.conf) $(wildcard conf/mods/*.load)

$(INST_DIR)/.httpd-setup: \
		$(INST_DIR)/.httpd-installed \
		conf/httpd.conf \
		conf/modules.conf \
		$(wildcard conf/sites/*.conf) \
        $(wildcard conf/mods/*.*) \
		$(INST_DIR)/conf/test.local.key \
		$(INST_DIR)/conf/test.local.pem
	@echo -n setup httpd locally...
	@mkdir -p $(INST_DIR)/conf/sites
	@mkdir -p $(INST_DIR)/conf/mods
	@for file in $(SETUP_FILES); do \
		sed -e "s,SUBST_SERVER_ROOT_SUBST,$(BLD_PREFIX),g" \
			-e "s,SUBST_SERVER_NAME_SUBST,$(shell hostname -f),g" \
			-e "s,SUBST_PORT_HTTP_SUBST,$(HTTP_PORT),g" \
			-e "s,SUBST_PORT_HTTPS_SUBST,$(HTTPS_PORT),g" \
			< $$file > $(INST_DIR)/$$file; \
	done
	@echo done.
	@touch $(INST_DIR)/.httpd-setup

$(INST_DIR)/conf/test.local.key:
	openssl genrsa -out $(INST_DIR)/conf/test.local.key 4096

$(INST_DIR)/conf/test.local.pem: \
		$(INST_DIR)/conf/test.local.key \
		conf/x509.input
	openssl req -new -x509 -key $(INST_DIR)/conf/test.local.key -out $(INST_DIR)/conf/test.local.pem -days 1095 < conf/x509.input

################################################################################
# Install the local httpd for our tests
#
$(INST_DIR)/.httpd-installed:
	@echo "you need to have httpd installed locally."
	@exit 1

################################################################################
# Install the local curl
#
$(INST_DIR)/.curl-installed:
	@echo "you need to have curl installed locally."
	@exit 1
