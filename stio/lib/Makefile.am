AUTOMAKE_OPTIONS = nostdinc

CPPFLAGS_ALL_COMMON = \
	-I$(abs_builddir) \
	-I$(abs_srcdir) \
	-I$(includedir)	

LDFLAGS_ALL_COMMON = -L$(abs_builddir) -L$(libdir)

##################################################
# MAIN LIBRARY 
##################################################

CPPFLAGS_LIB_COMMON = $(CPPFLAGS_ALL_COMMON) $(LTDLINCL)
LDFLAGS_LIB_COMMON = $(LDFLAGS_ALL_COMMON) -version-info 1:0:0 -no-undefined
LIBADD_LIB_COMMON = $(LIBM) $(LIBLTDL) 

#pkgincludedir = $(includedir)
#pkglibdir = $(libdir)
#pkgbindir = $(bindir)

include_HEADERS = \
	stio-cfg.h \
	stio-cmn.h \
	stio-pro.h \
	stio-sck.h \
	stio.h

lib_LTLIBRARIES = libstio.la
libstio_la_SOURCES = \
	stio-prv.h \
	stio.c \
	stio-pro.c \
	stio-sck.c \
	stio-tim.c \
	stio-tmr.c
libstio_la_CPPFLAGS = $(CPPFLAGS_LIB_COMMON)
libstio_la_LDFLAGS = $(LDFLAGS_LIB_COMMON)
libstio_la_LIBADD = $(LIBADD_LIB_COMMON)

bin_PROGRAMS = stio
stio_SOURCES = main.c
stio_CPPFLAGS = $(CPPFLAGS_LIB_COMMON)
stio_LDFLAGS = $(LDFLAGS_LIB_COMMON)
stio_LDADD = $(LIBADD_LIB_COMMON) -lstio 

install-data-hook:
	@echo "#ifndef _STIO_CFG_H_" > "$(DESTDIR)$(includedir)/stio-cfg.h"
	@echo "#define _STIO_CFG_H_" >> "$(DESTDIR)$(includedir)/stio-cfg.h"
	@$(EGREP) "#define[ ]+STIO_" "$(abs_builddir)/stio-cfg.h" >> "$(DESTDIR)$(includedir)/stio-cfg.h" 
	@echo "#endif" >> "$(DESTDIR)$(includedir)/stio-cfg.h"
	@$(RM) "$(DESTDIR)$(includedir)/stio-cfg.h.in"
	@$(SED) 's|/\*#define STIO_HAVE_CFG_H\*/|#define STIO_HAVE_CFG_H|' "$(srcdir)/stio-cmn.h" > "$(DESTDIR)$(includedir)/stio-cmn.h"

uninstall-hook:
	@$(RM) "$(DESTDIR)$(includedir)/stio-cfg.h"

