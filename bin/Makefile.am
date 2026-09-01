AUTOMAKE_OPTIONS = nostdinc

CPPFLAGS_COMMON = \
	-I$(abs_builddir) \
	-I$(abs_builddir)/../lib  \
	-I$(abs_srcdir) \
	-I$(abs_srcdir)/../lib \
	-I$(includedir)	
CFLAGS_COMMON =
LDFLAGS_COMMON = -L$(abs_builddir)/../lib -L$(libdir) 
LIBADD_COMMON = ../lib/libhio.la 

if ENABLE_ALL_STATIC
LDFLAGS_ALL_STATIC ?= -all-static
LIBADD_ALL_STATIC ?= $(ALL_STATIC_LIBS)
else
LDFLAGS_ALL_STATIC = 
LIBADD_ALL_STATIC =
endif

bin_PROGRAMS = hio-execd
hio_execd_SOURCES = execd.c
hio_execd_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_execd_CFLAGS = $(CFLAGS_COMMON)
hio_execd_LDFLAGS = $(LDFLAGS_COMMON)
hio_execd_LDADD = $(LIBADD_COMMON) 

if HAVE_X11_LIB
bin_PROGRAMS += hio-te
hio_te_SOURCES = te.c 
hio_te_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_te_CFLAGS = $(CFLAGS_COMMON)
hio_te_LDFLAGS = $(LDFLAGS_COMMON)
hio_te_LDADD = $(LIBADD_COMMON) -lX11
endif

bin_PROGRAMS += hio-untar
hio_untar_SOURCES = untar.c
hio_untar_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_untar_CFLAGS = $(CFLAGS_COMMON)
hio_untar_LDFLAGS = $(LDFLAGS_COMMON)
hio_untar_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += hio-webs
hio_webs_SOURCES = webs.c
hio_webs_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_webs_CFLAGS = $(CFLAGS_COMMON)
hio_webs_LDFLAGS = $(LDFLAGS_COMMON) $(LDFLAGS_ALL_STATIC)
hio_webs_LDADD = $(LIBADD_COMMON) $(LIBADD_ALL_STATIC)


# -------------------------------------------------

## [NOTE] hio-t01 through hio-t05 were retired. they exercised the library by
## printing what happened and leaving a human to judge it, and none of them was
## built by the default target or run by anything. what each uniquely covered
## now has a test that judges for itself:
##
##   t01  pipe, thread, process devices, dns client  -> t/t-014.c
##        icmp                                       -> t/t-013.c
##   t02  hio_dev_timedwrite                         -> t/t-008.c
##   t03  the json reader                            -> t/t-015.c
##   t04  the mariadb device                         -> t/t-016.c
##   t05  the pty device                             -> t/t-014.c
##
## everything else they touched - sockets, tls, the http tasks - was already
## covered elsewhere by the time they were removed.
