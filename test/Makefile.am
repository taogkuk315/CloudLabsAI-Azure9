# Copyright 2018 greenbytes GmbH (https://www.greenbytes.de)
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

SERVER_DIR     = @SERVER_DIR@
GEN            = gen


$(SERVER_DIR)/conf/ssl/valid_pkey.pem:
	@mkdir -p $(SERVER_DIR)/conf/ssl
	openssl genrsa -out $(SERVER_DIR)/conf/ssl/valid_pkey.pem 2048

$(SERVER_DIR)/conf/ssl/valid_cert.req: \
		$(SERVER_DIR)/conf/ssl/valid_pkey.pem \
		e2e/ssl/req_template.conf
	openssl req -new -key $(SERVER_DIR)/conf/ssl/valid_pkey.pem \
		-out $(SERVER_DIR)/conf/ssl/valid_cert.req \
		-config e2e/ssl/req_template.conf

$(SERVER_DIR)/conf/ssl/valid_cert.pem: \
		$(SERVER_DIR)/conf/ssl/valid_pkey.pem \
		$(SERVER_DIR)/conf/ssl/valid_cert.req
	openssl x509 -req -sha256 -days 1095 \
		-signkey $(SERVER_DIR)/conf/ssl/valid_pkey.pem \
		-in $(SERVER_DIR)/conf/ssl/valid_cert.req \
		-out $(SERVER_DIR)/conf/ssl/valid_cert.pem \
		-extfile e2e/ssl/req_exts.conf -extensions v3_req_ext

$(SERVER_DIR)/htdocs/.copied: \ 
            $(wildcard $(SERVER_DIR)/htdocs/*.*) \
            $(wildcard $(SERVER_DIR)/htdocs/*/*.*)
	@mkdir -p $(SERVER_DIR)/htdocs
	@cp -r e2e/htdocs/* $(SERVER_DIR)/htdocs 
	@chmod +x $(SERVER_DIR)/htdocs/cgi/*.* 

$(SERVER_DIR)/.test-setup: \
                $(wildcard e2e/conf/*) \
                $(SERVER_DIR)/htdocs/.copied \
		$(SERVER_DIR)/conf/ssl/valid_cert.pem
	@mkdir -p $(SERVER_DIR)/conf/ssl
	@mkdir -p $(SERVER_DIR)/logs
	rsync -a --exclude="*.in" e2e/conf/*.* $(SERVER_DIR)/conf
	@touch $(SERVER_DIR)/.test-setup


test: $(SERVER_DIR)/.test-setup
	cd e2e && py.test

clean-local:
	rm -rf *.pyc __pycache__
	rm -rf $(GEN)
