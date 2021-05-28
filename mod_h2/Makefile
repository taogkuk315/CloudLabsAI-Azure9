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




INST_DIR     = ../gen/install

APXS    = $(INST_DIR)/bin/apxs
APXS_FLAGS =

LIBTOOL = libtool

C_SRC   = \
        h2_bucket.c \
        h2_bucket_queue.c \
        h2_config.c \
        h2_ctx.c \
        h2_frame.c \
        h2_io.c \
        h2_queue.c \
        h2_response.c \
        h2_session.c \
        h2_stream.c \
        h2_stream_set.c \
        h2_task.c \
        h2_task_input.c \
        h2_task_output.c \
        h2_tls.c \
        h2_util.c \

CPP_SRC =

SRC     = $(C_SRC) $(CPP_SRC)
OBJ     = $(C_SRC:%.c=%.o) $(CPP_SRC:%.cc=%.o)
LOBJ    = $(OBJ:%.o=%.lo)

INCLUDES=-I.

LIBS    = -L$(INST_DIR)/lib -lnghttp2

# use the following rules in order to build .o files from .c and .cpp files
.cc.o : $(wildcard *.h)
	$(LIBTOOL) --mode=compile $(CXX) $(CFLAGS) $(INCLUDES) -c $<

.cpp.o : $(wildcard *.h)
	$(CXX) $(CFLAGS) $(INCLUDES) -c $<

.c.o : $(wildcard *.h)
	$(APXS) $(INCLUDES) -o $@ $(INCLUDES) -c $<

all: build

build: mod_h2.la

mod_h2.la : mod_h2.c $(OBJ) Makefile
	$(APXS) $(APXS_FLAGS) $(INCLUDES) -o $@ -c mod_h2.c $(LOBJ) $(LIBS)

install: mod_h2.la
	$(APXS) -i mod_h2.la

clean:
	rm -f *.o *.la *.lo *.slo

start: $(INST_DIR)/.test-setup
	@$(INST_DIR)/bin/apachectl restart

stop:
	@$(INST_DIR)/bin/apachectl stop

