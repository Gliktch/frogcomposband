MKPATH=../mk/
include $(MKPATH)buildsys.mk

# import source list
include Makefile.src
INCLUDE = $(HDRS)
OBJECTS = $(ANGFILES) $(CFILES) $(ZFILES)
SRCS    = ${OBJECTS:.o=.c} ${MAINFILES:.o=.c}
PROG    = $(PROGNAME)$(PROG_SUFFIX)
# VERSION := $(shell ../scripts/version.sh)
ifneq (${VERSION},)
	CFLAGS += -DBUILD_ID=${VERSION}
endif

CFLAGS += -I. -std=c99 -O0 -Wdeclaration-after-statement -fno-omit-frame-pointer

MANIFEST_ENABLED =
ifneq ($(MANIFEST),)
	CFLAGS += -DMANIFEST
	MANIFEST_ENABLED = 1
endif
ifneq ($(findstring -DMANIFEST,$(CFLAGS)),)
	MANIFEST_ENABLED = 1
endif
CLEAN = frogcomposband.o $(OBJECTS) win/frogcomposband.res
DISTCLEAN = autoconf.h

export CFLAGS LDFLAGS LIBS

ifneq ($(MANIFEST_ENABLED),)
	MANIFEST_SRC = manifest.c
	MANIFEST_OBJ = manifest.o
	OBJECTS += $(MANIFEST_OBJ)
	SRCS += $(MANIFEST_SRC)
	CLEAN += $(MANIFEST_SRC)

.PHONY: manifest-force
$(MANIFEST_SRC): manifest-force
	@sh ../mk/gen_manifest.sh $@ ..
endif

$(PROG): $(OBJECTS) $(MAINFILES)
	$(CC) -o $@ $(OBJECTS) $(MAINFILES) $(LDFLAGS) $(LDADD) $(LIBS)
	@printf "%10s %-20s\n" LINK $@

$(PROGNAME).res: $(PROGNAME).rc
	$(RC) $< -O coff -o $@

splint:
	splint -f .splintrc ${OBJECTS:.o=.c} main.c main-gcu.c
