

APR_VERSION = 1.5.1
APR_DIR = apr-$(APR_VERSION)
APR_TAR = $(APR_DIR).tar.gz
APR_URL = http://apache.mirror.iphh.net//apr/$(APR_TAR)

APR-UTIL_VERSION = 1.5.4
APR-UTIL_DIR = apr-util-$(APR-UTIL_VERSION)
APR-UTIL_TAR = $(APR-UTIL_DIR).tar.gz
APR-UTIL_URL = http://apache.mirror.iphh.net//apr/$(APR-UTIL_TAR)

HTTPD_VERSION = 2.4.10
HTTPD_DIR = httpd-$(HTTPD_VERSION)
HTTPD_TAR = httpd-$(HTTPD_VERSION).tar.gz
HTTPD_URL = https://archive.apache.org/dist/httpd/$(HTTPD_TAR)

PCRE_VERSION = 8.36
PCRE_DIR = pcre-$(PCRE_VERSION)
PCRE_TAR = $(PCRE_DIR).tar.gz
PCRE_URL = http://sourceforge.net/projects/pcre/files/pcre/$(PCRE_VERSION)/$(PCRE_TAR)

OPENSSL_DIR = openssl-1.0.2-beta3
OPENSSL_TAR = $(OPENSSL_DIR).tar.gz
OPENSSL_URL = https://www.openssl.org/source/$(OPENSSL_TAR)


GEN = gen

CURL_OPTS=--progress-bar

OS=$(shell uname -s)

HTTPD_CONF = --enable-mpms-shared=all
HTTPD_DEPS = $(GEN)/$(HTTPD_DIR)/.apr-extracted

# For OS X, we drag our own APR and PCRE lib in as the one installed is not
# recognized by the httpd configure (and pbly rightly so).
# On other OS, we expect a proper pcre installation
#
ifeq ($(OS),Darwin)
	# we need our own apr + apr-util
	HTTPD_DEPS     += $(GEN)/$(HTTPD_DIR)/.apr-extracted
	HTTPD_CONF     += --with-included-apr --with-crypto
	# we need our own pcre
	HTTPD_DEPS     += $(GEN)/$(BLD_DIR)/.pcre-installed
	HTTPD_CONF     += --with-pcre=$(BLD_PREFIX)
endif

OPENSSL_VERSION = $(shell openssl version -v | sed -e 's/OpenSSL *//g' -e 's/[a-z]* .*//g')

ifeq ($(OPENSSL_VERSION), $(filter $(OPENSSL_VERSION),0.9.7 0.9.8 1.0.0))
	# Very old openssl without alpn or npn support installed, need a newer one
	HTTPD_DEPS     += $(GEN)/$(BLD_DIR)/.openssl-installed
	HTTPD_CONF     += --with-ssl=$(BLD_PREFIX)
	ifeq ($(OS),Darwin)
	OPENSSL_CONF    = debug-darwin64-x86_64-cc
	endif
endif


BLD_DIR    = build
BLD_PREFIX = $(PWD)/$(GEN)/$(BLD_DIR)

HTTP_PORT = 12345
HTTPS_PORT = 12346

all: install

dirs:
	@mkdir -p $(GEN)/build

clean:
	@rm -rf $(GEN)/$(PCRE_DIR) $(GEN)/$(OPENSSL_DIR) $(GEN)/$(HTTPD_DIR) $(GEN)/build

distclean:
	@rm -rf $(GEN)

install:  $(GEN)/$(BLD_DIR)/.httpd-installed

setup: $(GEN)/$(BLD_DIR)/.httpd-setup

start: $(GEN)/$(BLD_DIR)/.httpd-setup
	@$(GEN)/$(BLD_DIR)/bin/apachectl restart

stop:
	@$(GEN)/$(BLD_DIR)/bin/apachectl stop

openssl:
	echo $(OPENSSL_VERSION) $(OPENSSL_URL)

copy-mod_ssl:
	@cp mod_ssl-alpn/*.[ch] $(GEN)/$(HTTPD_DIR)/modules/ssl
	@rm -f $(GEN)/$(HTTPD_DIR)/.httpd-built

################################################################################
# Test the local httpd
#
test: \
		$(GEN)/$(BLD_DIR)/.httpd-setup
	@rm -rf $(GEN)/test/tmp
	@mkdir -p $(GEN)/test/tmp
	@$(GEN)/$(BLD_DIR)/bin/apachectl restart
	@echo -n checking index.html over http...
	@curl -sk --resolv test.local:$(HTTP_PORT):127.0.0.1 http://test.local:$(HTTP_PORT)/index.html > $(GEN)/test/tmp/http_index.html
	@diff  $(GEN)/$(BLD_DIR)/htdocs/index.html $(GEN)/test/tmp/http_index.html
	@echo ok.
	@echo -n checking index.html over https with HTTP/1...
	@curl -sk --http1.1 --resolv test.local:$(HTTPS_PORT):127.0.0.1 https://test.local:$(HTTPS_PORT)/index.html > $(GEN)/test/tmp/https_index.html
	@diff  $(GEN)/$(BLD_DIR)/htdocs/index.html $(GEN)/test/tmp/https_index.html
	@echo ok.
	@echo -n checking index.html over https with h2...
	@curl -sk --http2.0 --resolv test.local:$(HTTPS_PORT):127.0.0.1 https://test.local:$(HTTPS_PORT)/index.html > $(GEN)/test/tmp/h2_index.html || bash -c "echo -n failed as expected..."
	@echo ok.
	@$(GEN)/$(BLD_DIR)/bin/apachectl stop


################################################################################
# Setup the local httpd for our tests
#
SETUP_FILES = $(wildcard conf/*.conf) $(wildcard conf/sites/*.conf)

$(GEN)/$(BLD_DIR)/.httpd-setup: \
		$(GEN)/$(BLD_DIR)/.httpd-installed \
		conf/httpd.conf conf/modules.conf \
		$(wildcard conf/sites/*.conf) \
		$(GEN)/$(BLD_DIR)/conf/test.local.key \
		$(GEN)/$(BLD_DIR)/conf/test.local.pem
	@echo -n setup httpd locally...
	@mkdir -p $(GEN)/$(BLD_DIR)/conf/sites
	@mkdir -p $(GEN)/$(BLD_DIR)/conf/mods
	@for file in $(SETUP_FILES); do \
		sed -e "s,SUBST_SERVER_ROOT_SUBST,$(BLD_PREFIX),g" \
			-e "s,SUBST_PORT_HTTP_SUBST,$(HTTP_PORT),g" \
			-e "s,SUBST_PORT_HTTPS_SUBST,$(HTTPS_PORT),g" \
			< $$file > $(GEN)/$(BLD_DIR)/$$file; \
	done
	@echo done.
	@touch $(GEN)/$(BLD_DIR)/.httpd-setup

$(GEN)/$(BLD_DIR)/conf/test.local.key:
	openssl genrsa -out $(GEN)/$(BLD_DIR)/conf/test.local.key 4096

$(GEN)/$(BLD_DIR)/conf/test.local.pem: \
		$(GEN)/$(BLD_DIR)/conf/test.local.key \
		conf/x509.input
	openssl req -new -x509 -key $(GEN)/$(BLD_DIR)/conf/test.local.key -out $(GEN)/$(BLD_DIR)/conf/test.local.pem -days 1095 < conf/x509.input

################################################################################
# Install the local httpd for our tests
#
$(GEN)/$(BLD_DIR)/.httpd-installed:  $(GEN)/$(HTTPD_DIR)/.httpd-built
	@echo -n installing httpd locally...
	@cd $(GEN)/$(HTTPD_DIR)/ && make install >> ../build.log
	@echo done.
	@touch $(GEN)/$(BLD_DIR)/.httpd-installed


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
$(GEN)/$(BLD_DIR)/.openssl-installed: \
		$(GEN)/$(OPENSSL_DIR)/.openssl-built
	@echo -n installing openssl locally, may take some time...
	@cd $(GEN)/$(OPENSSL_DIR) && make install_sw >> ../build.log
	@echo done.
	@touch $(GEN)/$(BLD_DIR)/.openssl-installed

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
$(GEN)/$(BLD_DIR)/.pcre-installed: \
		$(GEN)/$(PCRE_DIR)/.pcre-built
	@echo -n installing pcre locally...
	@cd $(GEN)/$(PCRE_DIR) && make install >> ../build.log
	@echo done.
	@touch $(GEN)/$(BLD_DIR)/.pcre-installed

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
