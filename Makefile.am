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

LDADD_COMMON = @DRM_LIBS@ @X11_LIBS@ util/libutil.la
CFLAGS = @DRM_CFLAGS@ @X11_CFLAGS@ @WARN_CFLAGS@ -I$(top_srcdir)/util

fliptest_SOURCES = fliptest.c
fliptest_LDADD = $(LDADD_COMMON) util/libutil.la

dmabuftest_SOURCES = dmabuftest.c
dmabuftest_LDADD = $(LDADD_COMMON) util/libutil.la
