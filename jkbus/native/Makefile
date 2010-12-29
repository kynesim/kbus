O ?= .
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
	$(CC) -o $(O)/lib/$(LIB_NAME) $(SO_FLAGS) $(OBJS) $(O)/libkbus/libkbus.a $(CFLAGS)

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
