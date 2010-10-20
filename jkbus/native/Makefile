CC=gcc
JAVA_HOME ?= /usr/lib/jvm/default-java/

LIB_NAME=libjkbus.so

SRCS=jkbus_ksock.c
LOCATED_SRCS==$(SRCS:%=src/%)
OBJS=$(SRCS:%.c=obj/%.o)

SO_FLAGS=-shared -Wl,-soname,$(LIB_NAME)

CFLAGS=-I$(JAVA_HOME)/include -I../java/include -Wall -Werror

all: dirs $(OBJS) bin/$(LIB_NAME)

bin/$(LIB_NAME): $(OBJS)
	$(CC) -o bin/$(LIB_NAME) $(SO_FLAGS) $(OBJS) $(CFLAGS)


obj/%.o: src/%.c
	$(CC) -c -o $@ $^ $(CFLAGS)

make clean:
	rm -rf bin
	rm -rf obj

dirs:
	mkdir -p bin
	mkdir -p obj