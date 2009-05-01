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

CC=gcc
LD=ld
WARNING_FLAGS=-Wall -Werror
DEBUG_FLAGS=
LD_LIB_FLAGS=-shared
INCLUDE_FLAGS=-I ..

OBJS=libkbus.o
DEPS=kbus.h

SONAME=libkbus.so


all: $(SONAME)

libkbus.o: $(DEPS)
	$(CC) $(INCLUDE_FLAGS) $(WARNING_FLAGS) -fPIC -c libkbus.c


$(SONAME): $(OBJS)
	$(LD) $(LD_LIB_FLAGS) -soname $(SONAME) -o $(SONAME) -lc $^

clean:
	if [ -e libkbus.so ]; then rm libkbus.so; fi
	if [ -e libkbus.o  ]; then rm libkbus.o;  fi