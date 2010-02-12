# Makefile for the KBUS kernel module

# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is the KBUS Lightweight Linux-kernel mediated
# message system
#
# The Initial Developer of the Original Code is Kynesim, Cambridge UK.
# Portions created by the Initial Developer are Copyright (C) 2009
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Kynesim, Cambridge UK
#   gb <gb@kynesim.co.uk>
#
# ***** END LICENSE BLOCK *****

ifdef CROSS_COMPILE
CC=$(CROSS_COMPILE)gcc
LD=$(CROSS_COMPILE)gcc		# because gcc knows where to find libc...
else
CC=gcc
LD=gcc
endif

# Note we assume a traditional Linux style environment in our flags
WARNING_FLAGS=-Wall -Werror
DEBUG_FLAGS=
LD_SHARED_FLAGS=-shared
INCLUDE_FLAGS=-I ..
CFLAGS=-fPIC -g

ifeq ($(O),)
	TGTDIR=.
else
	TGTDIR=$(O)/libkbus
endif

SRCS=libkbus.c limpet.c
OBJS=$(SRCS:%.c=$(TGTDIR)/%.o)
DEPS=kbus.h limpet.h

SHARED_NAME=libkbus.so
STATIC_NAME=libkbus.a
SHARED_TARGET=$(TGTDIR)/$(SHARED_NAME)
STATIC_TARGET=$(TGTDIR)/$(STATIC_NAME)

.PHONY: all
all: dirs $(SHARED_TARGET) $(STATIC_TARGET)

.PHONY: install
install:
	-mkdir -p $(DESTDIR)/lib
	-mkdir -p $(DESTDIR)/include/libkbus
	install -m 0644 kbus.h $(DESTDIR)/include/libkbus/kbus.h
	install -m 0755 $(SHARED_TARGET) $(DESTDIR)/lib/$(SHARED_NAME)
	install -m 0755 $(STATIC_TARGET) $(DESTDIR)/lib/$(STATIC_NAME)

.PHONY: dirs
dirs:
	-mkdir -p $(TGTDIR)

$(TGTDIR)/%.o: %.c
	$(CC) $(INCLUDE_FLAGS) $(CFLAGS) -o $@ $(WARNING_FLAGS) -c $^

$(TGTDIR)/%.o: $(DEPS)


$(SHARED_TARGET): $(OBJS)
	echo Objs = $(OBJS)
	$(LD) $(LD_SHARED_FLAGS) -soname=$(SHARED_NAME) -o $(SHARED_TARGET) $(OBJS) -lc

$(STATIC_TARGET): $(STATIC_TARGET)($(OBJS))

.PHONY: clean
clean:
	rm -f $(TGTDIR)/*.o $(SHARED_TARGET) $(STATIC_TARGET)
