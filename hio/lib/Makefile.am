AUTOMAKE_OPTIONS = nostdinc

CPPFLAGS_ALL_COMMON = \
	-I$(abs_builddir) \
	-I$(abs_srcdir) \
	-I$(includedir)	

CFLAGS_ALL_COMMON  =
LDFLAGS_ALL_COMMON = -L$(abs_builddir) -L$(libdir)

EXTRA_DIST = \
	hio-utl.h.m4 \
	utl-str.c.m4 \
	utl-str.m4

##################################################
# MAIN LIBRARY 
##################################################

CPPFLAGS_LIB_COMMON = $(CPPFLAGS_ALL_COMMON)
CFLAGS_LIB_COMMON = $(CFLAGS_ALL_COMMON)
LDFLAGS_LIB_COMMON = $(LDFLAGS_ALL_COMMON) -version-info 1:0:0 -no-undefined

# $(LIBM) here for quadmath_snprintf() in libquadmath
# TODO: make this selective by moving HIO_ENABLE_FLTFMT to configure.ac
LIBADD_LIB_COMMON = $(LIBM)

#pkgincludedir = $(includedir)
#pkglibdir = $(libdir)
#pkgbindir = $(bindir)

# Never list hio-cfg.h in include_HEADERS.
include_HEADERS = \
	hio-chr.h \
	hio-cmn.h \
	hio-dhcp.h \
	hio-dns.h \
	hio-ecs.h \
	hio-fmt.h \
	hio-htb.h \
	hio-htrd.h \
	hio-htre.h \
	hio-http.h \
	hio-json.h \
	hio-md5.h \
	hio-nwif.h \
	hio-opt.h \
	hio-pac1.h \
	hio-path.h \
	hio-pipe.h \
	hio-pro.h \
	hio-rad.h \
	hio-sck.h \
	hio-skad.h \
	hio-thr.h \
	hio-upac.h \
	hio-utl.h \
	hio.h

lib_LTLIBRARIES = libhio.la
libhio_la_SOURCES = \
	chr.c \
	dhcp-svr.c \
	dns.c \
	dns-cli.c \
	ecs.c \
	ecs-imp.h \
	err.c \
	fmt.c \
	fmt-imp.h \
	htb.c \
	htrd.c \
	htre.c \
	http.c \
	http-cgi.c \
	http-fil.c \
	http-prv.h \
	http-svr.c \
	http-thr.c \
	http-txt.c \
	json.c \
	hio-prv.h \
	hio.c \
	md5.c \
	nwif.c \
	opt.c \
	opt-imp.h \
	path.c \
	pipe.c \
	pro.c \
	rad-msg.c \
	sck.c \
	skad.c \
	sys.c \
	sys-ass.c \
	sys-err.c \
	sys-log.c \
	sys-mux.c \
	sys-prv.h \
	sys-tim.c \
	thr.c \
	uch-case.h \
	uch-prop.h \
	tmr.c \
	utf8.c \
	utl.c \
	utl-siph.c \
	utl-str.c


libhio_la_CPPFLAGS = $(CPPFLAGS_LIB_COMMON)
libhio_la_CFLAGS = $(CFLAGS_LIB_COMMON)
libhio_la_LDFLAGS = $(LDFLAGS_LIB_COMMON)
libhio_la_LIBADD = $(LIBADD_LIB_COMMON) $(SSL_LIBS) $(SOCKET_LIBS) $(SENDFILE_LIBS)

if ENABLE_MARIADB
include_HEADERS += hio-mar.h
libhio_la_SOURCES += mar.c mar-cli.c
libhio_la_CFLAGS += $(MARIADB_CFLAGS)
libhio_la_LDFLAGS += $(MARIADB_LDFLAGS)
libhio_la_LIBADD += $(MARIADB_LIBS)
endif

if ENABLE_SSL
libhio_la_LIBADD += $(SSL_LIBS)
endif

install-data-hook:
	@echo "#ifndef _HIO_CFG_H_" > "$(DESTDIR)$(includedir)/hio-cfg.h"
	@echo "#define _HIO_CFG_H_" >> "$(DESTDIR)$(includedir)/hio-cfg.h"
	@$(EGREP) "#define[ ]+HIO_" "$(abs_builddir)/hio-cfg.h" >> "$(DESTDIR)$(includedir)/hio-cfg.h" 
	@echo "#endif" >> "$(DESTDIR)$(includedir)/hio-cfg.h"
	@rm -f "$(DESTDIR)$(includedir)/hio-cfg.h.in"
	@$(SED) 's|/\*#define HIO_HAVE_CFG_H\*/|#define HIO_HAVE_CFG_H|' "$(srcdir)/hio-cmn.h" > "$(DESTDIR)$(includedir)/hio-cmn.h"

uninstall-hook:
	@rm -f "$(DESTDIR)$(includedir)/hio-cfg.h"

$(srcdir)/hio-utl.h: utl-str.m4 hio-utl.h.m4
	m4 -I$(srcdir) $(srcdir)/hio-utl.h.m4 > $(srcdir)/hio-utl.h

# do NOT put $(srcdir) before utl-str.c
utl-str.c: utl-str.m4 utl-str.c.m4 
	m4 -I$(srcdir) $(srcdir)/utl-str.c.m4 > $(srcdir)/utl-str.c

