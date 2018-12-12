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
	mio-cfg.h \
	mio-cmn.h \
	mio-pro.h \
	mio-sck.h \
	mio.h

lib_LTLIBRARIES = libmio.la
libmio_la_SOURCES = \
	mio-prv.h \
	mio.c \
	mio-pro.c \
	mio-sck.c \
	mio-tim.c \
	mio-tmr.c \
	mio-utl.c \
	utf8.c
libmio_la_CPPFLAGS = $(CPPFLAGS_LIB_COMMON)
libmio_la_LDFLAGS = $(LDFLAGS_LIB_COMMON)
libmio_la_LIBADD = $(LIBADD_LIB_COMMON) $(SSL_LIBS)

bin_PROGRAMS = mio
mio_SOURCES = main.c
mio_CPPFLAGS = $(CPPFLAGS_LIB_COMMON)
mio_LDFLAGS = $(LDFLAGS_LIB_COMMON)
mio_LDADD = $(LIBADD_LIB_COMMON) -lmio 

install-data-hook:
	@echo "#ifndef _MIO_CFG_H_" > "$(DESTDIR)$(includedir)/mio-cfg.h"
	@echo "#define _MIO_CFG_H_" >> "$(DESTDIR)$(includedir)/mio-cfg.h"
	@$(EGREP) "#define[ ]+MIO_" "$(abs_builddir)/mio-cfg.h" >> "$(DESTDIR)$(includedir)/mio-cfg.h" 
	@echo "#endif" >> "$(DESTDIR)$(includedir)/mio-cfg.h"
	@$(RM) "$(DESTDIR)$(includedir)/mio-cfg.h.in"
	@$(SED) 's|/\*#define MIO_HAVE_CFG_H\*/|#define MIO_HAVE_CFG_H|' "$(srcdir)/mio-cmn.h" > "$(DESTDIR)$(includedir)/mio-cmn.h"

uninstall-hook:
	@$(RM) "$(DESTDIR)$(includedir)/mio-cfg.h"

