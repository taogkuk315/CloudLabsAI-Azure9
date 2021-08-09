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



APR_VERSION  = 1.5.1
APR_DIR      = apr-$(APR_VERSION)
APR_TAR      = $(APR_DIR).tar.gz
APR_URL      = http://apache.mirror.iphh.net//apr/$(APR_TAR)

APR-UTIL_VERSION = 1.5.4
APR-UTIL_DIR = apr-util-$(APR-UTIL_VERSION)
APR-UTIL_TAR = $(APR-UTIL_DIR).tar.gz
APR-UTIL_URL = http://apache.mirror.iphh.net//apr/$(APR-UTIL_TAR)

HTTPD_VERSION = 2.4.12
HTTPD_DIR    = httpd-$(HTTPD_VERSION)
HTTPD_TAR    = httpd-$(HTTPD_VERSION).tar.gz
HTTPD_URL    = https://archive.apache.org/dist/httpd/$(HTTPD_TAR)

PCRE_VERSION = 8.36
PCRE_DIR     = pcre-$(PCRE_VERSION)
PCRE_TAR     = $(PCRE_DIR).tar.gz
PCRE_URL     = http://sourceforge.net/projects/pcre/files/pcre/$(PCRE_VERSION)/$(PCRE_TAR)

OPENSSL_DIR  = openssl-1.0.2
OPENSSL_TAR  = $(OPENSSL_DIR).tar.gz
OPENSSL_URL  = https://www.openssl.org/source/$(OPENSSL_TAR)


GEN          = gen
INST_DIR     = ../gen/install
BLD_PREFIX   = $(shell dirname $$PWD)/gen/install

CURL_OPTS    = --progress-bar

OS           = $(shell uname -s)

HTTPD_CONF   = --enable-mpms-shared=all
HTTPD_DEPS   = $(GEN)/$(HTTPD_DIR)/.apr-extracted

# For OS X, we drag our own APR and PCRE lib in as the one installed is not
# recognized by the httpd configure (and pbly rightly so).
# On other OS, we expect a proper pcre installation
#
ifeq ($(OS),Darwin)
	# we need our own apr + apr-util
	HTTPD_DEPS  += $(GEN)/$(HTTPD_DIR)/.apr-extracted
	HTTPD_CONF  += --with-included-apr --with-crypto
	# we need our own pcre
	HTTPD_DEPS  += $(INST_DIR)/.pcre-installed
	HTTPD_CONF  += --with-pcre=$(BLD_PREFIX)
endif

OPENSSL_VERSION = $(shell openssl version -v | sed -e 's/OpenSSL *//g' -e 's/[a-z]* .*//g')

ifeq ($(OPENSSL_VERSION), $(filter $(OPENSSL_VERSION),0.9.7 0.9.8 1.0.0))
	# Very old openssl without alpn or npn support installed, need a newer one
	HTTPD_DEPS  += $(INST_DIR)/.openssl-installed
	HTTPD_CONF  += --with-ssl=$(BLD_PREFIX)
	ifeq ($(OS),Darwin)
	OPENSSL_CONF = darwin64-x86_64-cc
	endif
endif


HTTP_PORT = 12345
HTTPS_PORT = 12346

all: install

dirs:
	@mkdir -p $(GEN)/build

clean:
	@rm -rf $(GEN)/$(PCRE_DIR) $(GEN)/$(OPENSSL_DIR) $(GEN)/$(HTTPD_DIR) $(GEN)/build

distclean:
	@rm -rf $(GEN)

install:  $(INST_DIR)/.httpd-installed

setup: $(INST_DIR)/.httpd-setup

start: $(INST_DIR)/.httpd-setup
	@$(INST_DIR)/bin/apachectl restart

stop:
	@$(INST_DIR)/bin/apachectl stop

copy-mod_ssl:
	@cp mod_ssl-alpn/*.[ch] $(GEN)/$(HTTPD_DIR)/modules/ssl
	@rm -f $(GEN)/$(HTTPD_DIR)/.httpd-built

################################################################################
# Install the local httpd
#
$(INST_DIR)/.httpd-installed:  $(GEN)/$(HTTPD_DIR)/.httpd-built
	@echo -n installing httpd locally...
	@cd $(GEN)/$(HTTPD_DIR)/ && make install >> ../build.log
	@echo done.
	@touch $(INST_DIR)/.httpd-installed


################################################################################
# Build the local, patched httpd
#
$(GEN)/$(HTTPD_DIR)/.httpd-built: \
		$(GEN)/$(HTTPD_DIR)/.httpd-configured
	@echo -n building httpd...
	@cd $(GEN)/$(HTTPD_DIR)/ && make >> ../build.log
	@echo done.
	@touch $(GEN)/$(HTTPD_DIR)/.httpd-built

################################################################################
# Configure the local httpd sources
#
$(GEN)/$(HTTPD_DIR)/configure: 
	cd $(GEN)/$(HTTPD_DIR)/ && ./buildconf

$(GEN)/$(HTTPD_DIR)/.httpd-configured: \
		$(HTTPD_DEPS) \
		$(GEN)/$(HTTPD_DIR)/configure \
		$(GEN)/$(HTTPD_DIR)/.httpd-patched
	@echo -n configuring httpd...
	cd $(GEN)/$(HTTPD_DIR)/ && \
	./configure --prefix=$(BLD_PREFIX) $(HTTPD_CONF) >> ../build.log
	@echo done.
	@touch $(GEN)/$(HTTPD_DIR)/.httpd-configured

################################################################################
# Patch the local httpd sources
#
$(GEN)/$(HTTPD_DIR)/.httpd-patched: \
		$(GEN)/$(HTTPD_DIR)/.httpd-extracted
	@echo applying patches...
	@cd gen/$(HTTPD_DIR) && patch -p0 < ../../patches/alpn-2.4.x.unified.diff.patch
	@echo httpd patched.
	@touch $(GEN)/$(HTTPD_DIR)/.httpd-patched

################################################################################
# Extract apache source tree
#
$(GEN)/$(HTTPD_DIR)/.httpd-extracted: \
		$(GEN)/$(HTTPD_TAR)
	@rm -rf $(GEN)/$(HTTPD_DIR)
	@echo -n extracting httpd packages...
	@cd $(GEN) && tar xfz $(HTTPD_TAR)
	@echo done.
	@touch $(GEN)/$(HTTPD_DIR)/.httpd-extracted

################################################################################
# Extract apr + apr-util into apache source tree (if needed)
#
$(GEN)/$(HTTPD_DIR)/.apr-extracted: \
		$(GEN)/$(HTTPD_DIR)/.httpd-extracted \
		$(GEN)/$(APR_TAR) $(GEN)/$(APR-UTIL_TAR)
	@rm -rf $(GEN)/$(HTTPD_DIR)/srclib/apr $(GEN)/$(HTTPD_DIR)/srclib/apr-util
	@echo -n extracting apr packages...
	@cd gen/$(HTTPD_DIR)/srclib; tar xfz ../../$(APR_TAR) && mv $(APR_DIR) apr
	@cd gen/$(HTTPD_DIR)/srclib; tar xfz ../../$(APR-UTIL_TAR) && mv $(APR-UTIL_DIR) apr-util
	@echo done.
	@touch $(GEN)/$(HTTPD_DIR)/.apr-extracted

################################################################################
# Retrieve apache sources (and apr + apr-util if needed)
#
$(GEN)/$(HTTPD_TAR):
	@mkdir -p $(GEN)
	curl $(CURL_OPTS) $(HTTPD_URL) > $(GEN)/$(HTTPD_TAR)

$(GEN)/$(APR_TAR):
	@mkdir -p $(GEN)
	curl $(CURL_OPTS) $(APR_URL) > $(GEN)/$(APR_TAR)

$(GEN)/$(APR-UTIL_TAR):
	@mkdir -p $(GEN)
	curl $(CURL_OPTS) $(APR-UTIL_URL) > $(GEN)/$(APR-UTIL_TAR)

################################################################################
# Build + install a local opensll library (if needed)
#
$(INST_DIR)/.openssl-installed: \
		$(GEN)/$(OPENSSL_DIR)/.openssl-built
	@echo -n installing openssl locally, may take some time...
	@cd $(GEN)/$(OPENSSL_DIR) && make install_sw >> ../build.log
	@echo done.
	@touch $(INST_DIR)/.openssl-installed

$(GEN)/$(OPENSSL_DIR)/.openssl-built: \
		$(GEN)/$(OPENSSL_DIR)/.openssl-configured
	@echo -n building openssl locally...
	@cd $(GEN)/$(OPENSSL_DIR) && make >> ../build.log
	@echo done.
	@touch $(GEN)/$(OPENSSL_DIR)/.openssl-built

$(GEN)/$(OPENSSL_DIR)/.openssl-configured: $(GEN)/$(OPENSSL_TAR)
	@rm -rf $(GEN)/$(OPENSSL_DIR)
	@cd $(GEN) && tar xfz $(OPENSSL_TAR)
	@echo -n configuring openssl...
	cd $(GEN)/$(OPENSSL_DIR) && ./Configure --openssldir=$(BLD_PREFIX) $(OPENSSL_CONF) >> ../build.log
	@echo done.
	@touch $(GEN)/$(OPENSSL_DIR)/.openssl-configured

$(GEN)/$(OPENSSL_TAR):
	@mkdir -p $(GEN)
	curl $(CURL_OPTS) $(OPENSSL_URL) > $(GEN)/$(OPENSSL_TAR)

################################################################################
# Build + install a local pcre library (if needed)
#
$(INST_DIR)/.pcre-installed: \
		$(GEN)/$(PCRE_DIR)/.pcre-built
	@echo -n installing pcre locally...
	@cd $(GEN)/$(PCRE_DIR) && make install >> ../build.log
	@echo done.
	@touch $(INST_DIR)/.pcre-installed

$(GEN)/$(PCRE_DIR)/.pcre-built: \
		$(GEN)/$(PCRE_DIR)/.pcre-configured
	@echo -n building pcre locally...
	@cd $(GEN)/$(PCRE_DIR) && make >> ../build.log
	@echo done.
	@touch $(GEN)/$(PCRE_DIR)/.pcre-built

$(GEN)/$(PCRE_DIR)/.pcre-configured: $(GEN)/$(PCRE_TAR)
	@rm -rf $(GEN)/$(PCRE_DIR)
	@cd $(GEN) && tar xfz $(PCRE_TAR)
	@echo -n configuring pcre...
	@cd $(GEN)/$(PCRE_DIR) && ./configure --prefix=$(BLD_PREFIX) >> ../build.log
	@echo done.
	@touch $(GEN)/$(PCRE_DIR)/.pcre-configured

$(GEN)/$(PCRE_TAR): packages/$(PCRE_TAR)
	@mkdir -p $(GEN)
	@cp packages/$(PCRE_TAR) $(GEN)
