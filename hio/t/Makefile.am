AUTOMAKE_OPTIONS = nostdinc

CPPFLAGS_COMMON = \
	-I$(abs_builddir) \
	-I$(abs_builddir)/../lib \
	-I$(abs_srcdir) \
	-I$(abs_srcdir)/../lib \
	-I$(includedir)
CFLAGS_COMMON =
LDFLAGS_COMMON=-L$(abs_builddir)/../lib  -L$(libdir)
## place $(LIBM)  here as all programs below are C only programs linked
## against the C/C++ hybrid library. Read comments in ../bin/Makefile.am
## for more information.
LIBADD_COMMON = ../lib/libhio.la $(LIBM)

check_SCRIPTS = 
##noinst_SCRIPTS = $(check_SCRIPTS)
EXTRA_DIST = $(check_SCRIPTS)

check_PROGRAMS = t-001 t-002

t_001_SOURCES = t-001.c t.h
t_001_CPPFLAGS = $(CPPFLAGS_COMMON)
t_001_CFLAGS = $(CFLAGS_COMMON)
t_001_LDFLAGS = $(LDFLAGS_COMMON)
t_001_LDADD = $(LIBADD_COMMON)

t_002_SOURCES = t-002.c t.h
t_002_CPPFLAGS = $(CPPFLAGS_COMMON)
t_002_CFLAGS = $(CFLAGS_COMMON)
t_002_LDFLAGS = $(LDFLAGS_COMMON)
t_002_LDADD = $(LIBADD_COMMON)


TESTS = $(check_PROGRAMS) $(check_SCRIPTS)

