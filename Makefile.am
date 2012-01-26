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

SUBDIRS = util
bin_PROGRAMS = fliptest dmabuftest

if ENABLE_DCE
bin_PROGRAMS += viddec3test
endif

LDADD_COMMON = util/libutil.la @DRM_LIBS@ @X11_LIBS@ @DCE_LIBS@
CFLAGS = @DRM_CFLAGS@ @X11_CFLAGS@ @DCE_CFLAGS@ @WARN_CFLAGS@ -I$(top_srcdir)/util

fliptest_SOURCES = fliptest.c
fliptest_LDADD = $(LDADD_COMMON)

dmabuftest_SOURCES = dmabuftest.c
dmabuftest_LDADD = $(LDADD_COMMON)

if ENABLE_DCE
viddec3test_SOURCES = viddec3test.c
viddec3test_LDADD = $(LDADD_COMMON)
endif
