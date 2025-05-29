# Copyright 2019 greenbytes GmbH (https://www.greenbytes.de)
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

SUBDIRS        = mod_h2test

SERVER_DIR     = @SERVER_DIR@
GEN            = gen

$(SERVER_DIR)/htdocs/.copied: \
            $(wildcard e2e/htdocs/*) \
            $(wildcard e2e/htdocs/*/*) \
            $(wildcard e2e/htdocs/*/*/*)
	@mkdir -p $(SERVER_DIR)/htdocs
	@cp -r e2e/htdocs/* $(SERVER_DIR)/htdocs 
	@chmod +x $(SERVER_DIR)/htdocs/cgi/*.* 

.PHONY: mod_h2test
mod_h2test:
	$(MAKE) -C mod_h2test

$(SERVER_DIR)/.test-setup: \
                mod_h2test \
                $(wildcard e2e/conf/*) \
                $(SERVER_DIR)/htdocs/.copied \
		$(CERTIFICATES)
	@mkdir -p $(SERVER_DIR)/conf/ssl
	@mkdir -p $(SERVER_DIR)/logs
	rsync -a --exclude="*.in" e2e/conf/*.* $(SERVER_DIR)/conf
	@touch $(SERVER_DIR)/.test-setup


test: $(SERVER_DIR)/.test-setup
	pytest

loadtest: $(SERVER_DIR)/.test-setup
	python3 e2e/load_test.py

clean-local:
	rm -rf *.pyc __pycache__
	rm -rf $(GEN)
