
HTTP_PORT = 12345
HTTPS_PORT = 12346

GEN          = gen
INST_DIR     = gen/install
BLD_PREFIX   = $(PWD)/gen/install

OS           = $(shell uname -s)

SUB_DIRS     = nghttp2 httpd clients test

CURL         = $(INST_DIR)/bin/curl


.PHONY: test

all:
	$(foreach sd, $(SUB_DIRS), make -C $(sd); )

clean:
	@rm -rf $(GEN)

distclean:
	@rm -rf $(GEN)
	$(foreach sd, $(SUB_DIRS), make -C $(sd) distclean; )

start: $(INST_DIR)/.httpd-setup
	@$(INST_DIR)/bin/apachectl restart

stop:
	@$(INST_DIR)/bin/apachectl stop

test: \
		$(INST_DIR)/.httpd-installed \
		$(INST_DIR)/.curl-installed
	make -C test test


################################################################################
# Install the local httpd for our tests
#
$(INST_DIR)/.httpd-installed:
		$(INST_DIR)/.nghttp2-installed
	make -C httpd install

################################################################################
# Install the local curl
#
$(INST_DIR)/.curl-installed:
		$(INST_DIR)/.nghttp2-installed
	make -C clients install

################################################################################
# Install the local nghttp2
#
$(INST_DIR)/.nghttp2-installed:
	make -C nghttp2 install

