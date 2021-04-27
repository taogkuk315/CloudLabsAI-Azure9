
CURL_VERSION = 7.40.0
CURL_DIR     = curl-$(CURL_VERSION)
CURL_TAR     = $(CURL_DIR).tar.gz
CURL_URL     = http://curl.haxx.se/download/$(CURL_TAR)

GEN          = gen
INST_DIR     = ../gen/install
BLD_PREFIX   = $(shell dirname $$PWD)/gen/install

OS           = $(shell uname -s)

CURL_CONF    = --with-nghttp2=$(BLD_PREFIX)

all: install-curl

dirs:
	@mkdir -p $(GEN)/build

clean:
	@rm -rf $(GEN)/$(CURL_DIR) $(GEN)/build.log

distclean:
	@rm -rf $(GEN)

install-curl:  $(INST_DIR)/.curl-installed


################################################################################
# Install the local curl our tests
#
$(INST_DIR)/.curl-installed:  $(GEN)/$(CURL_DIR)/.curl-built
	@echo -n installing curl locally...
	@cd $(GEN)/$(CURL_DIR)/ && make install >> ../build.log
	@echo done.
	@touch $(INST_DIR)/.curl-installed


################################################################################
# Build the local curl
#
$(GEN)/$(CURL_DIR)/.curl-built: \
		$(GEN)/$(CURL_DIR)/.curl-configured
	@echo -n building curl...
	@cd $(GEN)/$(CURL_DIR)/ && make >> ../build.log
	@echo done.
	@touch $(GEN)/$(CURL_DIR)/.curl-built

################################################################################
# Configure the local curl sources
#

PKG_CONF_MSG = Unable to find pkg-config executable. You need pkg-config installed \
for curl to be built with nghttp2. On OS X try: \"brew install pkg-config\" \
on Linux you local package manager should be able to help.

$(GEN)/$(CURL_DIR)/.curl-configured: \
		$(CURL_DEPS) \
        $(INST_DIR)/.nghttp2-installed \
		$(GEN)/$(CURL_DIR)/.curl-extracted
	@echo -n configuring curl...
	@if test ! -x "$(shell type -p pkg-config)"; then echo $(PKG_CONF_MSG); exit 1; fi
	cd $(GEN)/$(CURL_DIR)/ && \
	PKG_CONFIG_LIBDIR=$(BLD_PREFIX)/lib/pkgconfig ./configure --prefix=$(BLD_PREFIX) $(CURL_CONF)
	@echo done.
	@touch $(GEN)/$(CURL_DIR)/.curl-configured

################################################################################
# Extract curl source tree
#
$(GEN)/$(CURL_DIR)/.curl-extracted: \
		$(GEN)/$(CURL_TAR)
	@rm -rf $(GEN)/$(CURL_DIR)
	@echo -n extracting curl packages...
	@cd $(GEN) && tar xfz $(CURL_TAR)
	@echo done.
	@touch $(GEN)/$(CURL_DIR)/.curl-extracted

################################################################################
# Retrieve curl sources, we use curl for that???
#
$(GEN)/$(CURL_TAR):
	@mkdir -p $(GEN)
	curl --progress-bar $(CURL_URL) > $(GEN)/$(CURL_TAR)


################################################################################
# build and install local nghttp2
#
$(INST_DIR)/.nghttp2-installed:
	@make -C ../nghttp2 install