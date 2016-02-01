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

pkgincludedir = $(includedir)
pkglibdir = $(libdir)
pkgbindir = $(bindir)

pkginclude_HEADERS = \
	stio-cfg.h \
	stio-cmn.h \
	stio-tcp.h \
	stio-udp.h \
	stio.h

pkglib_LTLIBRARIES = libstio.la
libstio_la_SOURCES = \
	stio-prv.h \
	stio.c \
	stio-sck.c \
	stio-tcp.c \
	stio-tim.c \
	stio-tmr.c \
	stio-udp.c
libstio_la_CPPFLAGS = $(CPPFLAGS_LIB_COMMON)
libstio_la_LDFLAGS = $(LDFLAGS_LIB_COMMON)
libstio_la_LIBADD = $(LIBADD_LIB_COMMON)

pkgbin_PROGRAMS = stio
stio_SOURCES = main.c
stio_CPPFLAGS = $(CPPFLAGS_LIB_COMMON)
stio_LDFLAGS = $(LDFLAGS_LIB_COMMON)
stio_LDADD = $(LIBADD_LIB_COMMON) -lstio 

install-data-hook:
	@echo "#ifndef _STIO_CFG_H_" > "$(DESTDIR)$(pkgincludedir)/stio-cfg.h"
	@echo "#define _STIO_CFG_H_" >> "$(DESTDIR)$(pkgincludedir)/stio-cfg.h"
	@$(EGREP) "#define[ ]+STIO_" "$(abs_builddir)/stio-cfg.h" >> "$(DESTDIR)$(pkgincludedir)/stio-cfg.h" 
	@echo "#endif" >> "$(DESTDIR)$(pkgincludedir)/stio-cfg.h"
	@$(RM) "$(DESTDIR)$(pkgincludedir)/stio-cfg.h.in"
	@$(SED) 's|/\*#define STIO_HAVE_CFG_H\*/|#define STIO_HAVE_CFG_H|' "$(srcdir)/stio-cmn.h" > "$(DESTDIR)$(pkgincludedir)/stio-cmn.h"

uninstall-hook:
	@$(RM) "$(DESTDIR)$(pkgincludedir)/stio-cfg.h"

