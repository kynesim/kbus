# O is the desired out-of-tree object directory.
# If set, we expect to find libkbus/libkbus.a within.
# If not set we build in-tree, and pick up libkbus.a from ../../libkbus/.
ifeq ($(O),)
O := .
BUILDROOT := ../..
else
BUILDROOT := $(O)
endif
CC ?= gcc
JAVA_HOME ?= /usr/lib/jvm/default-java/

LIB_NAME=libjkbus.so

SRCS=jkbus_ksock.c
LOCATED_SRCS==$(SRCS:%=src/%)
OBJS=$(SRCS:%.c=$(O)/obj/%.o)

SO_FLAGS=-shared -Wl,-soname,$(LIB_NAME) 

CFLAGS := -fPIC -DPIC -I$(JAVA_HOME)/include -I../java/include -I$(O)/include \
	 -I../../libkbus/ -I../../ -Wall -Werror 


.PHONY: all
all: dirs $(OBJS) $(O)/lib/$(LIB_NAME)

$(O)/lib/$(LIB_NAME): $(OBJS)
	$(CC) -o $(O)/lib/$(LIB_NAME) $(SO_FLAGS) $(OBJS) $(BUILDROOT)/libkbus/libkbus.a $(CFLAGS)

$(O)/obj/%.o: src/%.c
	$(CC) -c -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm -rf $(O)/lib/$(LIB_NAME)
	rm -rf $(O)/obj/*.o

.PHONY: dirs
dirs:
	mkdir -p $(O)/lib
	mkdir -p $(O)/obj

# End file.
