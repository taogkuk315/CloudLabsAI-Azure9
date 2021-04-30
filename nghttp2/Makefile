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


NGHTTP2_VERSION = 0.7.1
NGHTTP2_DIR     = nghttp2-$(NGHTTP2_VERSION)
NGHTTP2_TAR     = $(NGHTTP2_DIR).tar.gz
NGHTTP2_URL     = https://github.com/tatsuhiro-t/nghttp2/releases/download/v$(NGHTTP2_VERSION)/$(NGHTTP2_TAR)

GEN          = gen
INST_DIR     = ../gen/install
BLD_PREFIX   = $(shell dirname $$PWD)/gen/install

CURL_OPTS    = --progress-bar

OS           = $(shell uname -s)

all: install

dirs:
	@mkdir -p $(GEN)/build

clean:
	@rm -rf $(GEN)/$(NGHTTP2_DIR)

distclean:
	@rm -rf $(GEN)

install:  $(INST_DIR)/.nghttp2-installed


################################################################################
# Install the local nghttp2
#
$(INST_DIR)/.nghttp2-installed:  $(GEN)/$(NGHTTP2_DIR)/.nghttp2-built
	@echo -n installing nghttp2 locally...
	@cd $(GEN)/$(NGHTTP2_DIR)/ && make install >> ../build.log
	@echo done.
	@touch $(INST_DIR)/.nghttp2-installed


################################################################################
# Build the local nghttp2
#
$(GEN)/$(NGHTTP2_DIR)/.nghttp2-built: \
		$(GEN)/$(NGHTTP2_DIR)/.nghttp2-configured
	@echo -n building nghttp2...
	@cd $(GEN)/$(NGHTTP2_DIR)/ && make >> ../build.log
	@echo done.
	@touch $(GEN)/$(NGHTTP2_DIR)/.nghttp2-built

################################################################################
# Configure the local nghttp2 sources
#
$(GEN)/$(NGHTTP2_DIR)/.nghttp2-configured: \
		$(NGHTTP2_DEPS) \
		$(GEN)/$(NGHTTP2_DIR)/.nghttp2-extracted
	@echo -n configuring nghttp2...
	cd $(GEN)/$(NGHTTP2_DIR)/ && \
	./configure --prefix=$(BLD_PREFIX) $(NGHTTP2_CONF) >> ../build.log
	@echo done.
	@touch $(GEN)/$(NGHTTP2_DIR)/.nghttp2-configured

################################################################################
# Extract nghttp2 source tree
#
$(GEN)/$(NGHTTP2_DIR)/.nghttp2-extracted: \
		$(GEN)/$(NGHTTP2_TAR)
	@rm -rf $(GEN)/$(NGHTTP2_DIR)
	@echo -n extracting nghttp2 packages...
	@cd $(GEN) && tar xfz $(NGHTTP2_TAR)
	@echo done.
	@touch $(GEN)/$(NGHTTP2_DIR)/.nghttp2-extracted

################################################################################
# Retrieve nghttp2 sources
#
$(GEN)/$(NGHTTP2_TAR):
	@mkdir -p $(GEN)
	@cp $(NGHTTP2_TAR) $(GEN)
	#curl $(CURL_OPTS) $(NGHTTP2_URL) > $(GEN)/$(NGHTTP2_TAR)

