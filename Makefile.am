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

## assumes --enable-static --disable-shared, so bin/hio-webs is the program
## itself and libhio is linked into it. what is still missing from a FROM scratch
## image is the system libraries it links against and the dynamic loader, so each
## goes in at the path it is loaded from - there is no /etc/ld.so.cache in the
## image to redirect the search.
docker: all
	rm -rf hio-webs-root hio-webs.tar
	mkdir -p hio-webs-root/bin hio-webs-root/data
	cp bin/hio-webs hio-webs-root/bin/hio-webs
	ldd bin/hio-webs | $(AWK) '$$2 == "=>" { print $$3 } $$1 ~ /^\// { print $$1 }' | \
		while read -r so; do \
			mkdir -p "hio-webs-root$${so%/*}"; \
			cp -L "$$so" "hio-webs-root$$so" || exit 1; \
		done
	tar -C hio-webs-root -cvf hio-webs.tar .
	docker build -t hio:webs -f Dockerfile .
	rm -rf hio-webs-root hio-webs.tar
