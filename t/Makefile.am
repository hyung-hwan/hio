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

check_SCRIPTS = s-001.sh
EXTRA_DIST = $(check_SCRIPTS)

check_PROGRAMS = t-001 t-002 t-003 t-004 t-005

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

t_003_SOURCES = t-003.c t.h
t_003_CPPFLAGS = $(CPPFLAGS_COMMON)
t_003_CFLAGS = $(CFLAGS_COMMON)
t_003_LDFLAGS = $(LDFLAGS_COMMON)
t_003_LDADD = $(LIBADD_COMMON)

t_004_SOURCES = t-004.c t.h
t_004_CPPFLAGS = $(CPPFLAGS_COMMON)
t_004_CFLAGS = $(CFLAGS_COMMON)
t_004_LDFLAGS = $(LDFLAGS_COMMON)
t_004_LDADD = $(LIBADD_COMMON)

t_005_SOURCES = t-005.c t.h
t_005_CPPFLAGS = $(CPPFLAGS_COMMON)
t_005_CFLAGS = $(CFLAGS_COMMON)
t_005_LDFLAGS = $(LDFLAGS_COMMON)
t_005_LDADD = $(LIBADD_COMMON)

LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/ac/tap-driver.sh
TESTS = $(check_PROGRAMS) $(check_SCRIPTS)

TEST_EXTENSIONS = .sh
SH_LOG_DRIVER = $(LOG_DRIVER)
SH_LOG_COMPILER = /bin/sh
##SH_LOG_DRIVER_FLAGS =
##AM_SH_LOG_DRIVER_FLAGS =
