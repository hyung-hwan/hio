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

noinst_PROGRAMS = hio-t01
hio_t01_SOURCES = t01.c
hio_t01_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t01_CFLAGS = $(CFLAGS_COMMON)
hio_t01_LDFLAGS = $(LDFLAGS_COMMON)
hio_t01_LDADD = $(LIBADD_COMMON) 

noinst_PROGRAMS += hio-t02
hio_t02_SOURCES = t02.c
hio_t02_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t02_CFLAGS = $(CFLAGS_COMMON)
hio_t02_LDFLAGS = $(LDFLAGS_COMMON)
hio_t02_LDADD = $(LIBADD_COMMON)

noinst_PROGRAMS += hio-t03
hio_t03_SOURCES = t03.c
hio_t03_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t03_CFLAGS = $(CFLAGS_COMMON)
hio_t03_LDFLAGS = $(LDFLAGS_COMMON)
hio_t03_LDADD = $(LIBADD_COMMON)

noinst_PROGRAMS += hio-t04
hio_t04_SOURCES = t04.c
hio_t04_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t04_CFLAGS = $(CFLAGS_COMMON)
hio_t04_LDFLAGS = $(LDFLAGS_COMMON)
hio_t04_LDADD = $(LIBADD_COMMON)
if ENABLE_MARIADB
hio_t04_CFLAGS += $(MARIADB_CFLAGS)  
hio_t04_LDFLAGS += $(MARIADB_LDFLAGS)  
hio_t04_LDADD += $(MARIADB_LIBS)  
endif

noinst_PROGRAMS += hio-t05
hio_t05_SOURCES = t05.c
hio_t05_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t05_CFLAGS = $(CFLAGS_COMMON)
hio_t05_LDFLAGS = $(LDFLAGS_COMMON)
hio_t05_LDADD = $(LIBADD_COMMON)

noinst_PROGRAMS += hio-t06
hio_t06_SOURCES = t06.c
hio_t06_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t06_CFLAGS = $(CFLAGS_COMMON)
hio_t06_LDFLAGS = $(LDFLAGS_COMMON)
hio_t06_LDADD = $(LIBADD_COMMON)
