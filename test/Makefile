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

HOST_NAME    = test.example.org

HTTP_PORT    = 12345
HTTPS_PORT   = 12346

HTTP_AUTH    = $(HOST_NAME):$(HTTP_PORT)
HTTPS_AUTH   = $(HOST_NAME):$(HTTPS_PORT)

GEN          = gen
INST_DIR     = ../gen/install
BLD_PREFIX   = $(shell dirname $$PWD)/gen/install

OS           = $(shell uname -s)

CURL         = $(INST_DIR)/bin/curl
NGHTTP       = $(INST_DIR)/bin/nghttp
H2LOAD       = $(INST_DIR)/bin/h2load


all: test

clean:
	@rm -rf $(GEN) $(INST_DIR)/.testdocs-setup $(INST_DIR)/.test-setup

distclean:
	@rm -rf $(GEN)
	$(foreach sd, $(SUB_DIRS), make -C $(sd) distclean; )

start: $(INST_DIR)/.test-setup
	@ulimit -c unlimited; $(INST_DIR)/bin/apachectl restart

stop:
	@$(INST_DIR)/bin/apachectl stop


################################################################################
# Test the local httpd
#
test: \
		$(INST_DIR)/.test-setup \
		$(INST_DIR)/.curl-installed
	@rm -rf $(GEN)/tmp
	@mkdir -p $(GEN)/tmp
	@$(INST_DIR)/bin/apachectl restart
	@bash test_001.sh https://$(HTTPS_AUTH)
	@bash test_001.sh http://$(HTTP_AUTH)

################################################################################
# Load Test
#

$(GEN)/load-urls-1.txt: \
		load-urls-1.txt
	@mkdir -p $(GEN)/tmp
	@sed -e "s,SUBST_AUTH_SUBST,https://$(HTTPS_AUTH)," < load-urls-1.txt >$(GEN)/load-urls-1.txt

$(GEN)/load-urls-2.txt: \
		load-urls-1.txt
	@mkdir -p $(GEN)/tmp
	@sed -e "s,SUBST_AUTH_SUBST,http://$(HTTP_AUTH)," < load-urls-1.txt >$(GEN)/load-urls-2.txt

test1: \
		$(INST_DIR)/.test-setup \
		$(INST_DIR)/.curl-installed \
        $(GEN)/load-urls-1.txt
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 100000 -t 1 -c 1

loadtest: \
		$(INST_DIR)/.test-setup \
		$(INST_DIR)/.curl-installed \
        $(GEN)/load-urls-1.txt
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 1
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 2
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 3
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 4
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 5
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 6
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 7
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 8
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 9
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 20000 -t 1 -c 10


################################################################################
# Setup the local httpd for our tests
#
SETUP_FILES = $(wildcard conf/*.conf) $(wildcard conf/sites/*.conf) \
    $(wildcard conf/mods/*.conf) $(wildcard conf/mods/*.load)

$(INST_DIR)/.testdocs-setup: \
	$(wildcard conf/*) \
	$(wildcard htdocs/*/*)
	@echo -n setup htdocs locally...
	@cp -r htdocs/* $(INST_DIR)/htdocs
	@echo done.
	@touch $(INST_DIR)/.testdocs-setup

$(INST_DIR)/.test-setup: \
		$(INST_DIR)/.httpd-installed \
		conf/httpd.conf \
		conf/modules.conf \
		$(wildcard conf/sites/*.conf) \
        $(wildcard conf/mods/*.*) \
		$(INST_DIR)/conf/ssl/test.example.org.key \
		$(INST_DIR)/conf/ssl/test.example.org.pem \
		$(INST_DIR)/.testdocs-setup
	@echo -n setup httpd locally...
	@mkdir -p $(INST_DIR)/conf/sites
	@mkdir -p $(INST_DIR)/conf/mods
	@mkdir -p $(INST_DIR)/conf/ssl
	@for file in $(SETUP_FILES); do \
		sed -e "s,SUBST_SERVER_ROOT_SUBST,$(BLD_PREFIX),g" \
			-e "s,SUBST_SERVER_NAME_SUBST,$(shell hostname -f),g" \
			-e "s,SUBST_PORT_HTTP_SUBST,$(HTTP_PORT),g" \
			-e "s,SUBST_PORT_HTTPS_SUBST,$(HTTPS_PORT),g" \
			< $$file > $(INST_DIR)/$$file; \
	done
	@cp conf/ssl/*.* $(INST_DIR)/conf/ssl
	@echo done.
	@touch $(INST_DIR)/.test-setup

$(INST_DIR)/conf/ssl/test.example.org.key:
	@mkdir -p $(INST_DIR)/conf/ssl
	openssl genrsa -out $(INST_DIR)/conf/ssl/test.example.org.key 4096

$(INST_DIR)/conf/ssl/test.example.org.pem: \
		$(INST_DIR)/conf/ssl/test.example.org.key \
		conf/ssl/x509.input
	openssl req -new -x509 -key $(INST_DIR)/conf/ssl/test.example.org.key \
		-out $(INST_DIR)/conf/ssl/test.example.org.pem -days 1095 < conf/ssl/x509.input

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
