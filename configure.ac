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

AC_PREREQ([2.69])
AC_INIT([mod_h2], [0.1.0], [stefan.eissing@greenbytes.de])

LT_PREREQ([2.2.6])
LT_INIT()
dnl See versioning rule:
dnl  http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
AC_SUBST(LT_CURRENT, 11)
AC_SUBST(LT_REVISION, 0)
AC_SUBST(LT_AGE, 6)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([subdir-objects no-define])

AC_CONFIG_MACRO_DIR([m4])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_SRCDIR([mod_h2/h2_bucket.c])
AC_CONFIG_HEADERS([mod_h2/config.h])



AC_ARG_ENABLE([local], [AS_HELP_STRING([--enable-local],
    [Build a local installation [default=no]])],
    [case "${enableval}" in
        yes) build_mode=local ;;
        no)  build_mode=extern ;;
        *) AC_MSG_ERROR([bad value ${enableval} for --enable-local]) ;;
    esac],
    [build_mode=extern])

AC_ARG_WITH([apxs], [AS_HELP_STRING([--with-apxs],
    [Use APXS executable [default=check]])],
    [request_apxs=$withval], [request_apxs=check])


# Checks for programs.
AC_PROG_CC
AC_PROG_CC_STDC

AC_MSG_NOTICE("configuring for $build_mode build")

if test x"$build_mode" == "xlocal"; then
    if test x"$request_apxs" != "xcheck"; then
        AC_MSG_ERROR([when local mode is enabled, specifying apxs is not allowed])
    fi

    # easy, we know where everything will be
    #
    prefix="$PWD/gen/install"
    APXS="$prefix/bin/apxs"
    LDFLAGS="$LDFLAGS -L${prefix}/lib"
    LIBS="$LIBS -lnghttp2"
    CPPFLAGS="$CPPFLAGS -I${prefix}/include"

    # On Ubuntu, we have a propblem with APR 1.5.1 and system includes that
    # causes compilation to abort since no definition of PATH_MAX can be found.
    # In local build, we just define it therefore, as a quick workaround
    CPPFLAGS="$CPPFLAGS -DPATH_MAX=4096"
else
    # production, we need to find where the apxs is. which then
    # can tell us the various directories we need.
    #
    if test x"$request_apxs" == "xcheck"; then
        AC_PATH_PROG([APXS], [apxs])
        if test "x${APXS}" == "x"; then
            AC_MSG_ERROR("no APXS installation found")
        fi
    else
        APXS="$request_apxs"
        AC_MSG_NOTICE("using APXS=$APXS as configured")
    fi

    prefix="$($APXS -q prefix)"
    LDFLAGS="$LDFLAGS -L$($APXS -q libdir)"
    CPPFLAGS="$CPPFLAGS $($APXS -q includedir) -I$($APXS -q APR_INCLUDEDIR)"

    # We need nghttp2 to be in our link path, check for it.
    #
    AC_CHECK_LIB([nghttp2], [nghttp2_session_server_new2], ,
    [AC_MSG_ERROR("library nghttp2 not found")])

fi

# Checks for header files.
AC_CHECK_HEADERS([stddef.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT32_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([memmove memset strcasecmp strchr])

AC_CONFIG_FILES([mod_h2/Makefile])
AC_OUTPUT

AC_MSG_NOTICE([summary of build options:

    Version:        ${VERSION} shared $LT_CURRENT:$LT_REVISION:$LT_AGE
    Host type:      ${host}
    Build Type:     ${build_mode}
    Install prefix: ${prefix}
    APXS:           ${APXS}
    C compiler:     ${CC}
    CFLAGS:         ${CFLAGS}
    WARNCFLAGS:     ${WARNCFLAGS}
    LDFLAGS:        ${LDFLAGS}
    LIBS:           ${LIBS}
    CPPFLAGS:       ${CPPFLAGS}
    C preprocessor: ${CPP}
])
