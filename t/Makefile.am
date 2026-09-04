AUTOMAKE_OPTIONS = nostdinc

CPPFLAGS_COMMON = \
	-I$(abs_builddir) \
	-I$(abs_builddir)/../lib \
	-I$(abs_srcdir) \
	-I$(abs_srcdir)/../lib \
	-I$(includedir)
CFLAGS_COMMON =
LDFLAGS_COMMON=-L$(abs_builddir)/../lib  -L$(libdir)
## place $(LIBM) here as all programs below are C only programs linked
## against the C/C++ hybrid library. Read comments in ../bin/Makefile.am
## for more information.
LIBADD_COMMON = ../lib/libhio.la $(LIBM)


check_SCRIPTS = s-001.sh s-002.sh
EXTRA_DIST = $(check_SCRIPTS) tap.inc t-cgi.sh

## programs run as tests
TESTPROGS = t-001 t-002 t-003 t-004 t-005 t-006 t-007 t-008 t-009 t-010 t-011 t-012 t-013 t-014 t-015 t-016 t-017 t-018 t-019

## helpers the test scripts drive. built with the tests, but not tests
## themselves - they are servers and would never exit.
HELPERPROGS = fcgis httpecho httssvr slowclient sctpget

check_PROGRAMS = $(TESTPROGS) $(HELPERPROGS)

t_001_SOURCES = t-001.c tap.h
t_001_CPPFLAGS = $(CPPFLAGS_COMMON)
t_001_CFLAGS = $(CFLAGS_COMMON)
t_001_LDFLAGS = $(LDFLAGS_COMMON)
t_001_LDADD = $(LIBADD_COMMON)

t_002_SOURCES = t-002.c tap.h
t_002_CPPFLAGS = $(CPPFLAGS_COMMON)
t_002_CFLAGS = $(CFLAGS_COMMON)
t_002_LDFLAGS = $(LDFLAGS_COMMON)
t_002_LDADD = $(LIBADD_COMMON)

t_003_SOURCES = t-003.c tap.h
t_003_CPPFLAGS = $(CPPFLAGS_COMMON)
t_003_CFLAGS = $(CFLAGS_COMMON)
t_003_LDFLAGS = $(LDFLAGS_COMMON)
t_003_LDADD = $(LIBADD_COMMON)

t_004_SOURCES = t-004.c tap.h
t_004_CPPFLAGS = $(CPPFLAGS_COMMON)
t_004_CFLAGS = $(CFLAGS_COMMON)
t_004_LDFLAGS = $(LDFLAGS_COMMON)
t_004_LDADD = $(LIBADD_COMMON)

t_005_SOURCES = t-005.c tap.h
t_005_CPPFLAGS = $(CPPFLAGS_COMMON)
t_005_CFLAGS = $(CFLAGS_COMMON)
t_005_LDFLAGS = $(LDFLAGS_COMMON)
t_005_LDADD = $(LIBADD_COMMON)

## t-006/t-007/t-008 exercise the core loop itself and reach into
## hio-prv.h for the timer entry points, hence the extra include path.
t_006_SOURCES = t-006.c tap.h
t_006_CPPFLAGS = $(CPPFLAGS_COMMON)
t_006_CFLAGS = $(CFLAGS_COMMON)
t_006_LDFLAGS = $(LDFLAGS_COMMON)
t_006_LDADD = $(LIBADD_COMMON)

t_007_SOURCES = t-007.c tap.h
t_007_CPPFLAGS = $(CPPFLAGS_COMMON)
t_007_CFLAGS = $(CFLAGS_COMMON)
t_007_LDFLAGS = $(LDFLAGS_COMMON)
t_007_LDADD = $(LIBADD_COMMON)

t_008_SOURCES = t-008.c t-008-wq.inc tap.h
t_008_CPPFLAGS = $(CPPFLAGS_COMMON)
t_008_CFLAGS = $(CFLAGS_COMMON)
t_008_LDFLAGS = $(LDFLAGS_COMMON)
t_008_LDADD = $(LIBADD_COMMON)

## t-009 drives a tls client and a tls server in one loop. it skips itself
## if the openssl command is unavailable to generate a throwaway certificate.
t_009_SOURCES = t-009.c tap.h
t_009_CPPFLAGS = $(CPPFLAGS_COMMON)
t_009_CFLAGS = $(CFLAGS_COMMON)
t_009_LDFLAGS = $(LDFLAGS_COMMON)
t_009_LDADD = $(LIBADD_COMMON)

## t-010 feeds bytes straight into the http reader - no sockets, no loop.
t_010_SOURCES = t-010.c tap.h
t_010_CPPFLAGS = $(CPPFLAGS_COMMON)
t_010_CFLAGS = $(CFLAGS_COMMON)
t_010_LDFLAGS = $(LDFLAGS_COMMON)
t_010_LDADD = $(LIBADD_COMMON)

## t-011 pins the connect-readiness handling. it makes the race deterministic
## rather than waiting for a slow machine to expose it.
t_011_SOURCES = t-011.c tap.h
t_011_CPPFLAGS = $(CPPFLAGS_COMMON)
t_011_CFLAGS = $(CFLAGS_COMMON)
t_011_LDFLAGS = $(LDFLAGS_COMMON)
t_011_LDADD = $(LIBADD_COMMON)

## t-012 drives an sctp association. it skips itself where the build has no
## sctp support, which it discovers the same way a caller would.
t_012_SOURCES = t-012.c tap.h
t_012_CPPFLAGS = $(CPPFLAGS_COMMON)
t_012_CFLAGS = $(CFLAGS_COMMON)
t_012_LDFLAGS = $(LDFLAGS_COMMON)
t_012_LDADD = $(LIBADD_COMMON)

## t-013 drives the link-layer device types. they need CAP_NET_RAW, so it
## re-executes itself under 'unshare -r -n' where that is permitted - which also
## keeps its traffic off the real network - and skips where it is not.
t_013_SOURCES = t-013.c tap.h
t_013_CPPFLAGS = $(CPPFLAGS_COMMON)
t_013_CFLAGS = $(CFLAGS_COMMON)
t_013_LDFLAGS = $(LDFLAGS_COMMON)
t_013_LDADD = $(LIBADD_COMMON)

## t-014 covers the device types that had no test of their own - pipe, thread
## and process - and the dns client's failure path. it is what was worth keeping
## from bin/t01.c, which covered them by printing and leaving a human to judge.
t_014_SOURCES = t-014.c tap.h
t_014_CPPFLAGS = $(CPPFLAGS_COMMON)
t_014_CFLAGS = $(CFLAGS_COMMON)
t_014_LDFLAGS = $(LDFLAGS_COMMON)
t_014_LDADD = $(LIBADD_COMMON)

## t-015 covers the json reader. it is pure - no sockets, no privilege, no
## timing - so it is checked exactly, against the instruction stream the grammar
## says each document should produce. from bin/t03.c.
t_015_SOURCES = t-015.c tap.h
t_015_CPPFLAGS = $(CPPFLAGS_COMMON)
t_015_CFLAGS = $(CFLAGS_COMMON)
t_015_LDFLAGS = $(LDFLAGS_COMMON)
t_015_LDADD = $(LIBADD_COMMON)

## t-016 covers the mariadb device. a server cannot be assumed, so it tests what
## needs none - the failed connect and the escaping - and reaches for a real one
## only when the environment names it. skips entirely without --enable-mariadb.
## from bin/t04.c, which took connection details on the command line.
t_016_SOURCES = t-016.c tap.h
t_016_CPPFLAGS = $(CPPFLAGS_COMMON)
t_016_CFLAGS = $(CFLAGS_COMMON)
t_016_LDFLAGS = $(LDFLAGS_COMMON)
t_016_LDADD = $(LIBADD_COMMON)
if ENABLE_MARIADB
t_016_CFLAGS += $(MARIADB_CFLAGS)
t_016_LDFLAGS += $(MARIADB_LDFLAGS)
t_016_LDADD += $(MARIADB_LIBS)
endif

## t-017 covers the dhcpv4 message layer, which the server and the client both
## stand on. it is pure - a buffer in, a buffer out - so its failure modes are
## all reachable from a unit test.
t_017_SOURCES = t-017.c tap.h
t_017_CPPFLAGS = $(CPPFLAGS_COMMON)
t_017_CFLAGS = $(CFLAGS_COMMON)
t_017_LDFLAGS = $(LDFLAGS_COMMON)
t_017_LDADD = $(LIBADD_COMMON)

## t-018 drives the dhcp server's state machine directly through
## hio_svc_dhcs_process(), which does no i/o - so the protocol is exercised by
## handing it packets rather than by standing up a network.
t_018_SOURCES = t-018.c tap.h
t_018_CPPFLAGS = $(CPPFLAGS_COMMON)
t_018_CFLAGS = $(CFLAGS_COMMON)
t_018_LDFLAGS = $(LDFLAGS_COMMON)
t_018_LDADD = $(LIBADD_COMMON)

## t-019 covers the dhcpv6 message layer. dhcpv6 relaying is containment
## rather than a header field - the relayed message is the value of an option
## in the relaying one - so the nesting is what most of these cases are about.
t_019_SOURCES = t-019.c tap.h
t_019_CPPFLAGS = $(CPPFLAGS_COMMON)
t_019_CFLAGS = $(CFLAGS_COMMON)
t_019_LDFLAGS = $(LDFLAGS_COMMON)
t_019_LDADD = $(LIBADD_COMMON)

## a minimal FastCGI responder, driven by s-002.sh
fcgis_SOURCES = fcgis.c
fcgis_CPPFLAGS = $(CPPFLAGS_COMMON)
fcgis_CFLAGS = $(CFLAGS_COMMON)
fcgis_LDFLAGS = $(LDFLAGS_COMMON)
fcgis_LDADD = $(LIBADD_COMMON)

## a minimal http upstream for the proxy route, driven by s-002.sh
httpecho_SOURCES = httpecho.c
httpecho_CPPFLAGS = $(CPPFLAGS_COMMON)
httpecho_CFLAGS = $(CFLAGS_COMMON)
httpecho_LDFLAGS = $(LDFLAGS_COMMON)
httpecho_LDADD = $(LIBADD_COMMON)

## an http server exercising the htts task types, driven by s-002.sh
httssvr_SOURCES = httssvr.c
httssvr_CPPFLAGS = $(CPPFLAGS_COMMON)
httssvr_CFLAGS = $(CFLAGS_COMMON)
httssvr_LDFLAGS = $(LDFLAGS_COMMON)
httssvr_LDADD = $(LIBADD_COMMON)

## t-009 writes a throwaway certificate into the build directory and removes
## it on the way out. this catches the case where it dies before that.
CLEANFILES = t009-cert.pem t009-key.pem

## a deliberately slow http client used by the header-deadline test. plain
## posix sockets - it must not behave like an hio client.
slowclient_SOURCES = slowclient.c
slowclient_CPPFLAGS = $(CPPFLAGS_COMMON)
slowclient_CFLAGS = $(CFLAGS_COMMON)
slowclient_LDFLAGS = $(LDFLAGS_COMMON)
slowclient_LDADD = $(LIBADD_COMMON)

## a minimal http client that speaks sctp. curl cannot, so the harness needs
## this to reach the sctp listener at all.
sctpget_SOURCES = sctpget.c
sctpget_CPPFLAGS = $(CPPFLAGS_COMMON)
sctpget_CFLAGS = $(CFLAGS_COMMON)
sctpget_LDFLAGS = $(LDFLAGS_COMMON)
sctpget_LDADD = $(LIBADD_COMMON)

LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/ac/tap-driver.sh
TESTS = $(TESTPROGS) $(check_SCRIPTS)

TEST_EXTENSIONS = .sh
SH_LOG_DRIVER = $(LOG_DRIVER)
SH_LOG_COMPILER = /bin/sh
##SH_LOG_DRIVER_FLAGS =
##AM_SH_LOG_DRIVER_FLAGS =
