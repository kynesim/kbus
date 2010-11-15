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
#   Tony Ibbs <tibs@tonyibbs.co.uk>
#
# ***** END LICENSE BLOCK *****

ifdef CROSS_COMPILE
CXX=$(CROSS_COMPILE)g++
LD=$(CROSS_COMPILE)g++		# because gcc knows where to find libc...
else
CXX=g++
LD=g++
endif

# Note we assume a traditional Linux style environment in our flags
WARNING_FLAGS=-Wall -Werror
LD_SHARED_FLAGS+=-shared
INCLUDE_FLAGS=-I .. -I ../kbus
CXXFLAGS+=-fPIC -g $(WARNING_FLAGS) $(INCLUDE_FLAGS)

ifeq ($(O),)
	TGTDIR=.
else
	TGTDIR=$(O)/cppkbus
endif

SRCS=cppkbus.cpp
OBJS=$(SRCS:%.cpp=$(TGTDIR)/%.o)
DEPS=cppkbus.h kbus_defns.h

SHARED_NAME=libcppkbus.so
STATIC_NAME=libcppkbus.a
SHARED_TARGET=$(TGTDIR)/$(SHARED_NAME)
STATIC_TARGET=$(TGTDIR)/$(STATIC_NAME)

.PHONY: all
all: dirs $(SHARED_TARGET) $(STATIC_TARGET)

.PHONY: install
install:
	-mkdir -p $(DESTDIR)/lib
	-mkdir -p $(DESTDIR)/include/kbus
	install -m 0644 cppkbus.h   $(DESTDIR)/include/kbus/cppkbus.h
	install -m 0755 $(SHARED_TARGET) $(DESTDIR)/lib/$(SHARED_NAME)
	install -m 0755 $(STATIC_TARGET) $(DESTDIR)/lib/$(STATIC_NAME)

test:	test.cpp $(STATIC_TARGET)
	$(CXX)  $(CXXFLAGS) -o $@ $^

.PHONY: dirs
dirs:
	-mkdir -p $(TGTDIR)

$(TGTDIR)/%.o: %.cpp $(DEPS)
	$(CXX)  $(CXXFLAGS) -o $@ -c $^

$(SHARED_TARGET): $(OBJS)
	echo Objs = $(OBJS)
	$(LD) $(LD_SHARED_FLAGS) -o $(SHARED_TARGET) $(OBJS) -lc

$(STATIC_TARGET): $(STATIC_TARGET)($(OBJS))

.PHONY: clean
clean:
	rm -f $(TGTDIR)/*.o $(SHARED_TARGET) $(STATIC_TARGET)
	rm -f test
