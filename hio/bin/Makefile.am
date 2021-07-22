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

bin_PROGRAMS = hio-execd
hio_execd_SOURCES = execd.c
hio_execd_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_execd_CFLAGS = $(CFLAGS_COMMON)
hio_execd_LDFLAGS = $(LDFLAGS_COMMON)
hio_execd_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += hio-t01
hio_t01_SOURCES = t01.c
hio_t01_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t01_CFLAGS = $(CFLAGS_COMMON)
hio_t01_LDFLAGS = $(LDFLAGS_COMMON)
hio_t01_LDADD = $(LIBADD_COMMON) 

bin_PROGRAMS += hio-t02
hio_t02_SOURCES = t02.c
hio_t02_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t02_CFLAGS = $(CFLAGS_COMMON)
hio_t02_LDFLAGS = $(LDFLAGS_COMMON)
hio_t02_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += hio-t03
hio_t03_SOURCES = t03.c
hio_t03_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t03_CFLAGS = $(CFLAGS_COMMON)
hio_t03_LDFLAGS = $(LDFLAGS_COMMON)
hio_t03_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += hio-t04
hio_t04_SOURCES = t04.c
hio_t04_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t04_CFLAGS = $(CFLAGS_COMMON)
hio_t04_LDFLAGS = $(LDFLAGS_COMMON)
hio_t04_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += hio-t05
hio_t05_SOURCES = t05.c
hio_t05_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t05_CFLAGS = $(CFLAGS_COMMON)
hio_t05_LDFLAGS = $(LDFLAGS_COMMON)
hio_t05_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += hio-t06
hio_t06_SOURCES = t06.c
hio_t06_CPPFLAGS = $(CPPFLAGS_COMMON)
hio_t06_CFLAGS = $(CFLAGS_COMMON)
hio_t06_LDFLAGS = $(LDFLAGS_COMMON)
hio_t06_LDADD = $(LIBADD_COMMON)
