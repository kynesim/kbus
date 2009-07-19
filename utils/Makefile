# Some useful utilities for kbus

DESTDIR ?= ..

SRCS=kmsg.c
OBJS=$(SRCS:%.c=%.o)
TESTS=$(OBJS:%.o=%)

LOCATED_OBJS=$(OBJS:%.o=$(DESTDIR)/utils/%.o)
LOCATED_TESTS=$(TESTS:%=$(DESTDIR)/utils/%)

CFLAGS=-I$(DESTDIR)
LDFLAGS=-L$(DESTDIR)/libkbus
LIBS=-lkbus

all: $(LOCATED_TESTS)


$(DESTDIR)/utils/%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

$(DESTDIR)/utils/% : $(DESTDIR)/utils/%.o
	$(CC) -o $@ $(LDFLAGS) $^ $(LIBS)

$(DESTDIR)/utils/% : $(DESTDIR)/libkbus/libkbus.so

$(DESTDIR)/libkbus/libkbus.so: 
	$(MAKE) -C $(DESTDIR)/libkbus


# End file.

