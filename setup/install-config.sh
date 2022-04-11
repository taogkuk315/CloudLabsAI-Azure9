#!/bin/sh
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

SYSCONF="$1"
DESTDIR="$2"
A2ENMOD="$( type -p a2enmod )"

if [ -d "$DESTDIR" ]; then
	cat << EOF
  You need to add loading instructions to your httpd configruation
  in order to use mod_h2.
EOF
elif [ -d "$SYSCONF/mods-available" ]; then
    echo -n "installing mod_h2 config in $SYSCONF..."
    cp h2.conf h2.load "$SYSCONF/mods-available"
    echo "done."
    if [ -x "$A2ENMOD" ]; then
        echo -n "enabling mod_h2..."
        "$A2ENMOD" mod_h2
        echo "done."
    fi
else
    cat <<EOF
  This does not look like a apache2 installation, as in Ubuntu or
  other debian based systems. Therefore, the local files h2.load and
  h2.conf have *not* been installed.

  If you want to have the h2 module enabled in your apache installtion, you
  need to add
     LoadModule h2_module modules/mod_h2.so
  somewhere in your config files and add a line like
     H2Engine on
  whereever you want the module to be active (general server of specific
  virtual hosts).

EOF
fi

