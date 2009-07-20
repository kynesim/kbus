# Some useful utilities for kbus

SRCS=kmsg.c
OBJS=$(SRCS:%.c=%.o)
TESTS=$(OBJS:%.o=%)

LOCATED_OBJS=$(OBJS:%.o=$(O)/utils/%.o)
LOCATED_TESTS=$(TESTS:%=$(O)/utils/%)

CFLAGS=-I..
LDFLAGS=-L$(O)/libkbus
LIBS=-lkbus

all: dirs $(LOCATED_TESTS)

install:
	-mkdir -p $(DESTDIR)/bin
	install -m 0755 $(O)/utils/kmsg $(DESTDIR)/bin/kmsg

dirs:
	-mkdir -p $(O)/utils

$(O)/utils/%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

$(O)/utils/% : $(O)/utils/%.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

$(O)/utils/% : $(O)/libkbus/libkbus.so

$(O)/libkbus/libkbus.so: 
	$(MAKE) -C ../libkbus O=$(O) all

# Mustn't delete utils - remember that for in-place builds it's
# the source directory!
clean:
	rm -rf $(O)/utils/*.o $(O)/utils/kmsg

distclean: clean



# End file.

