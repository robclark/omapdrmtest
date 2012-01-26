#  
#  Copyright (C) 2011 Texas Instruments
#  Author: Rob Clark <rob.clark@linaro.org>
#  
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License version 2 as published by
#  the Free Software Foundation.
#  
#  This program is distributed in the hope that it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#  more details.
#  
#  You should have received a copy of the GNU General Public License along with
#  this program.  If not, see <http://www.gnu.org/licenses/>.
#  

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([omapdrmtest], [1.0.0], [https://www.ti.com], [omapdrmtest])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE

# Enable quiet compiles on automake 1.11.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Initialize libtool
AC_PROG_LIBTOOL

# Obtain compiler/linker options for depedencies
PKG_CHECK_MODULES(DRM, libdrm libdrm_omap)

# Check optional X11:
PKG_CHECK_MODULES(X11, x11 libdri2, [HAVE_X11=yes], [HAVE_X11=no])
if test "x$HAVE_X11" = "xyes"; then
	AC_DEFINE(HAVE_X11, 1, [Have X11 support])
else
	AC_MSG_WARN([No X11 support detected, disabling X11 support])
fi
AM_CONDITIONAL(ENABLE_X11, [test "x$HAVE_X11" = xyes])

# Check for libdce and libav..
PKG_CHECK_MODULES(DCE, libdce libavformat libavutil, [HAVE_DCE=yes], [HAVE_DCE=no])
if test "x$HAVE_DCE" = "xyes"; then
	AC_DEFINE(HAVE_DCE, 1, [Have DCE support])
	AC_MSG_NOTICE([Detected libdce and libavformat, building video codec tests])
else
	AC_MSG_WARN([No libdce and/or libavformat support detected, disabling video codec tests])
fi
AM_CONDITIONAL(ENABLE_DCE, [test "x$HAVE_DCE" = xyes])

dnl ===========================================================================
dnl check compiler flags
AC_DEFUN([LIBDRM_CC_TRY_FLAG], [
  AC_MSG_CHECKING([whether $CC supports $1])

  libdrm_save_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $1"

  AC_COMPILE_IFELSE([ ], [libdrm_cc_flag=yes], [libdrm_cc_flag=no])
  CFLAGS="$libdrm_save_CFLAGS"

  if test "x$libdrm_cc_flag" = "xyes"; then
    ifelse([$2], , :, [$2])
  else
    ifelse([$3], , :, [$3])
  fi
  AC_MSG_RESULT([$libdrm_cc_flag])
])

MAYBE_WARN="-Wall -Wextra \
-Wsign-compare -Werror-implicit-function-declaration \
-Wpointer-arith -Wwrite-strings -Wstrict-prototypes \
-Wnested-externs \
-Wpacked -Wswitch-enum -Wmissing-format-attribute \
-Wstrict-aliasing=2 -Winit-self -Wunsafe-loop-optimizations \
-Wdeclaration-after-statement -Wold-style-definition \
-Wno-missing-field-initializers -Wno-unused-parameter \
-Wno-attributes -Wno-long-long -Winline"

# invalidate cached value if MAYBE_WARN has changed
if test "x$libdrm_cv_warn_maybe" != "x$MAYBE_WARN"; then
        unset libdrm_cv_warn_cflags
fi
AC_CACHE_CHECK([for supported warning flags], libdrm_cv_warn_cflags, [
        echo
        WARN_CFLAGS=""

        # Some warning options are not supported by all versions of
        # gcc, so test all desired options against the current
        # compiler.
        #
        # Note that there are some order dependencies
        # here. Specifically, an option that disables a warning will
        # have no net effect if a later option then enables that
        # warnings, (perhaps implicitly). So we put some grouped
        # options (-Wall and -Wextra) up front and the -Wno options
        # last.

        for W in $MAYBE_WARN; do
                LIBDRM_CC_TRY_FLAG([$W], [WARN_CFLAGS="$WARN_CFLAGS $W"])
        done

        libdrm_cv_warn_cflags=$WARN_CFLAGS
        libdrm_cv_warn_maybe=$MAYBE_WARN

        AC_MSG_CHECKING([which warning flags were supported])])
WARN_CFLAGS="$libdrm_cv_warn_cflags"
AC_SUBST(WARN_CFLAGS)


AC_CONFIG_FILES([Makefile util/Makefile])
AC_OUTPUT
