AUTOMAKE_OPTION = foreign
ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST =

SUBDIRS = lib bin t
DIST_SUBDIRS = $(SUBDIRS) 

distclean-local:
	rm -rf $(top_srcdir)/autom4te.cache

######################################################################

rpm: dist-gzip
	mkdir -p "@abs_builddir@/pkgs/RPM/BUILD"
	mkdir -p "@abs_builddir@/pkgs/RPM/SOURCES"
	mkdir -p "@abs_builddir@/pkgs/RPM/SRPMS"
	mkdir -p "@abs_builddir@/pkgs/RPM/RPMS"
	cp @PACKAGE_NAME@-@PACKAGE_VERSION@.tar.gz "@abs_builddir@/pkgs/RPM/SOURCES"
	rpmbuild --define "_topdir @abs_builddir@/pkgs/RPM" -ba @abs_builddir@/pkgs/hio.spec --target=@build_cpu@

