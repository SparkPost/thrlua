CMOD_DIR=$(prefix)/lib/lua/@PACKAGE_VERMAJORMINOR@

SUBDIRS = src doc etc

EXTRA_DIST = rclua.spec COPYING HISTORY INSTALL README

install-data-local:
	$(mkdir_p) "$(DESTDIR)$(CMOD_DIR)"

.PHONY: rpm test
rpm: dist-bzip2
	for d in RPMS SRPMS SOURCES BUILD SPECS ; do \
		$(mkdir_p) $(srcdir)/rpm-build/$$d ; \
	done
	cp $(distdir).tar.bz2 $(srcdir)/rpm-build/SOURCES
	cd $(srcdir) ; rpmbuild \
		--define="_topdir `pwd`/rpm-build" \
		--define="_prefix $(prefix)" \
		--define="_exec_prefix $(exec_prefix)" \
		--define="_docdir $(prefix)/share/doc" \
		--define="_mandir $(mandir)" \
		--define="_infodir $(infodir)" \
		--define="_defaultdocdir $(prefix)/share/doc" \
		--define="EXTRA_RPM_DEPS @EXTRA_RPM_DEPS@" \
		-ba rclua.spec

clean-local:
	rm -rf rpm-build

test:
	./runtests
