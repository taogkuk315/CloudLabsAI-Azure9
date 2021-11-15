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

HOST_NAME    = test.example.org

HTTP_PORT    = 12345
HTTPS_PORT   = 12346

HTTP_AUTH    = $(HOST_NAME):$(HTTP_PORT)
HTTPS_AUTH   = $(HOST_NAME):$(HTTPS_PORT)

GEN          = gen
INST_DIR     = ../gen/install
BLD_PREFIX   = $(shell dirname $$PWD)/gen/install

SUB_DIRS     = clients

CURL         = $(INST_DIR)/bin/curl
NGHTTP       = $(INST_DIR)/bin/nghttp
H2LOAD       = $(INST_DIR)/bin/h2load


all: test

clean:
	@rm -rf $(GEN) $(INST_DIR)/.testdocs-setup $(INST_DIR)/.test-setup
	@make -C clients clean

distclean:
	@rm -rf $(GEN)
	$(foreach sd, $(SUB_DIRS), make -C $(sd) distclean; )

distdir:
	@mkdir -p $(distdir)
	@tar cf - Makefile conf htdocs clients/Makefile *.txt *.sh | (cd $(distdir) && tar xf - )
	@rm -f $(distdir)/conf/ssl/mod-h2.greebytes.de*
	@rm -f $(distdir)/conf/sites/mod-h2.greebytes.de.conf

start: $(INST_DIR)/.test-setup
	@$(INST_DIR)/bin/apachectl restart

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
	@bash test_001_get.sh https://$(HTTPS_AUTH)
	@bash test_001_get.sh http://$(HTTP_AUTH)
	@bash test_002_post.sh https://$(HTTPS_AUTH)
	@bash test_002_post.sh http://$(HTTP_AUTH)

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

loadtest: \
		$(INST_DIR)/.test-setup \
		$(INST_DIR)/.curl-installed \
        $(GEN)/load-urls-1.txt
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 200000 -t 1 -c 8
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 200000 -t 2 -c 8
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 200000 -t 3 -c 8
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 200000 -t 4 -c 8
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 200000 -t 5 -c 8
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 200000 -t 6 -c 8
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 200000 -t 7 -c 8
	@$(H2LOAD) -i $(GEN)/load-urls-1.txt -n 200000 -t 8 -c 8


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
	@chmod +x $$(find $(INST_DIR)/htdocs/test.example.org/ -name "*.py" )
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

$(INST_DIR)/conf/ssl/test.example.org.req: \
		$(INST_DIR)/conf/ssl/test.example.org.key
	openssl req -new -key $(INST_DIR)/conf/ssl/test.example.org.key \
		-out $(INST_DIR)/conf/ssl/test.example.org.req < conf/ssl/x509.input

$(INST_DIR)/conf/ssl/test.example.org.pem: \
		$(INST_DIR)/conf/ssl/test.example.org.key \
		$(INST_DIR)/conf/ssl/test.example.org.req \
		conf/ssl/extensions.conf \
		conf/ssl/x509.input
	openssl x509 -req -sha256 -days 1095 \
		-signkey $(INST_DIR)/conf/ssl/test.example.org.key \
		-in $(INST_DIR)/conf/ssl/test.example.org.req \
		-out $(INST_DIR)/conf/ssl/test.example.org.pem \
		-extfile conf/ssl/extensions.conf -extensions ssl_client < conf/ssl/x509.input


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
	make -C clients
