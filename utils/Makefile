# A useful utility for testing kbus
#
# Use ``O=<destination>`` for a remote build, ``STATIC=1`` for a static build,
# ``STATIC=0`` for a shared build. Static builds are default for local builds,
# shared builds default for remote builds.
#
ifdef CROSS_COMPILE
CC=$(CROSS_COMPILE)gcc
else
CC=gcc
endif

# Note we assume a traditional Linux style environment in our flags
CFLAGS=-I..
LDFLAGS=-L$(LIBKBUSDIR)
LIBS=-lkbus

ifeq ($(O),)
	TGTDIR=.
	LIBKBUSDIR=../libkbus
ifeq ($(STATIC),)
    STATIC=1
endif
else
	# Remote build, apparently wants to be shared
	TGTDIR=$(O)/utils
	LIBKBUSDIR=$(O)/libkbus
endif

ifeq ($(STATIC),1)
	LDFLAGS+=-static
	LIBDEPEND=$(LIBKBUSDIR)/libkbus.a
else
	LIBDEPEND=$(LIBKBUSDIR)/libkbus.so
endif

TARGET=$(TGTDIR)/kmsg

.PHONY: all
all: dirs $(TARGET)

.PHONY: dirs
dirs:
	-mkdir -p $(TGTDIR)

$(TARGET): kmsg.c $(LIBDEPEND)
	$(CC) kmsg.c -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

$(LIBDEPEND):
	$(MAKE) -C ../libkbus O=$(O) all

# Mustn't delete utils - remember that for in-place builds it's
# the source directory!
.PHONY: clean
clean:
	rm -rf $(TGTDIR)/*.o $(TGTDIR)/kmsg

.PHONY: distclean
distclean: clean

.PHONY: install
install:
	-mkdir -p $(DESTDIR)/bin
	install -m 0755 $(TGTDIR)/kmsg $(DESTDIR)/bin/kmsg
