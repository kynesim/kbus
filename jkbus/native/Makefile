CC=gcc
JAVA_HOME ?= /usr/lib/jvm/default-java/

LIB_NAME=libjkbus.so

SRCS=jkbus_ksock.c
LOCATED_SRCS==$(SRCS:%=src/%)
OBJS=$(SRCS:%.c=obj/%.o)

SO_FLAGS=-shared -Wl,-soname,$(LIB_NAME) 

CFLAGS=-I$(JAVA_HOME)/include -I../java/include -I../../libkbus/ -I../../ -Wall -Werror 

all: dirs $(OBJS) lib/$(LIB_NAME)

lib/$(LIB_NAME): $(OBJS)
	$(CC) -o lib/$(LIB_NAME) $(SO_FLAGS) $(OBJS) ../../libkbus/libkbus.a $(CFLAGS)


obj/%.o: src/%.c
	$(CC) -c -o $@ $^ $(CFLAGS)

make clean:
	rm -rf lib
	rm -rf obj

dirs:
	mkdir -p lib
	mkdir -p obj