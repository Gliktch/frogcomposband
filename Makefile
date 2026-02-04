MKPATH=mk/
include $(MKPATH)buildsys.mk

# Commit manifest automatically enabled on startup for development builds
MANIFEST ?= 1

SUBDIRS = src lib
CLEAN = config.status config.log *.dll *.exe

.PHONY: manual dist
TAG = frogcomposband-`git describe`
OUT = $(TAG).tar.gz

manual:

dist: manual
	git checkout-index --prefix=$(TAG)/ -a
	git describe > $(TAG)/version
	$(TAG)/autogen.sh
	rm -rf $(TAG)/autogen.sh $(TAG)/autom4te.cache
	#cp doc/manual.html doc/manual.pdf $(TAG)/doc/
	tar --exclude .gitignore --exclude *.dll -czvf $(OUT) $(TAG)
	rm -rf $(TAG)

install-extra:
	${MKDIR_P} ${DESTDIR}${vardatadir}/apex \
		${DESTDIR}${vardatadir}/data \
		${DESTDIR}${vardatadir}/save \
		${DESTDIR}${vardatadir}/user
	if [ "x$(SETEGID)" != "x" ]; then \
		chown -R root:$(SETEGID) ${DESTDIR}${vardatadir}/apex \
			${DESTDIR}${vardatadir}/data \
			${DESTDIR}${vardatadir}/save \
			${DESTDIR}${vardatadir}/user; \
		chmod -R g+w ${DESTDIR}${vardatadir}/apex \
			${DESTDIR}${vardatadir}/data \
			${DESTDIR}${vardatadir}/save \
			${DESTDIR}${vardatadir}/user; \
	fi
