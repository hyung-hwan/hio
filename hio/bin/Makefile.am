AUTOMAKE_OPTIONS = nostdinc

CPPFLAGS_COMMON = \
	-I$(abs_builddir) \
	-I$(abs_builddir)/../lib  \
	-I$(abs_srcdir) \
	-I$(abs_srcdir)/../lib \
	-I$(includedir)	
CFLAGS_COMMON =
LDFLAGS_COMMON = -L$(abs_builddir)/../lib -L$(libdir)
LIBADD_COMMON = ../lib/libmio.la

bin_PROGRAMS = mio-execd
mio_execd_SOURCES = execd.c
mio_execd_CPPFLAGS = $(CPPFLAGS_COMMON)
mio_execd_CFLAGS = $(CFLAGS_COMMON)
mio_execd_LDFLAGS = $(LDFLAGS_COMMON)
mio_execd_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += mio-t01
mio_t01_SOURCES = t01.c
mio_t01_CPPFLAGS = $(CPPFLAGS_COMMON)
mio_t01_CFLAGS = $(CFLAGS_COMMON)
mio_t01_LDFLAGS = $(LDFLAGS_COMMON)
mio_t01_LDADD = $(LIBADD_COMMON) 

bin_PROGRAMS += mio-t02
mio_t02_SOURCES = t02.c
mio_t02_CPPFLAGS = $(CPPFLAGS_COMMON)
mio_t02_CFLAGS = $(CFLAGS_COMMON)
mio_t02_LDFLAGS = $(LDFLAGS_COMMON)
mio_t02_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += mio-t03
mio_t03_SOURCES = t03.c
mio_t03_CPPFLAGS = $(CPPFLAGS_COMMON)
mio_t03_CFLAGS = $(CFLAGS_COMMON)
mio_t03_LDFLAGS = $(LDFLAGS_COMMON)
mio_t03_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += mio-t04
mio_t04_SOURCES = t04.c
mio_t04_CPPFLAGS = $(CPPFLAGS_COMMON)
mio_t04_CFLAGS = $(CFLAGS_COMMON)
mio_t04_LDFLAGS = $(LDFLAGS_COMMON)
mio_t04_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += mio-t05
mio_t05_SOURCES = t05.c
mio_t05_CPPFLAGS = $(CPPFLAGS_COMMON)
mio_t05_CFLAGS = $(CFLAGS_COMMON)
mio_t05_LDFLAGS = $(LDFLAGS_COMMON)
mio_t05_LDADD = $(LIBADD_COMMON)

bin_PROGRAMS += mio-t06
mio_t06_SOURCES = t06.c
mio_t06_CPPFLAGS = $(CPPFLAGS_COMMON)
mio_t06_CFLAGS = $(CFLAGS_COMMON)
mio_t06_LDFLAGS = $(LDFLAGS_COMMON)
mio_t06_LDADD = $(LIBADD_COMMON)
