# Global makefile for kbus
#
# You'll need GNU Make for this - or at least a make that supports
#  VPATH - sorry.
#
# To build kbus out of tree:
#
# $(MAKE) O=$(OBJECT_LOCATION)
# 
# To install kbus:
#
# $(MAKE) O=$(OBJECT_LOCATION) DESTDIR=$(DESTDIR) install
#
# DESTDIR will contain:
#
#  lib/libkbus.so
#  install/kbus/*.h
#  bin/kmsg
#  kmodules/kbus.ko
#

# If no object location is specified, it's here.
O ?= $(CURDIR)
DESTDIR ?= /usr/local

SUBDIRS=kbus libkbus utils

# The MAKE all here is important - there's a distinction in the kbus
# directory because the kernel makefile objects to building kernel 
# modules not in the directory of their sources. Sigh.
all:
	for i in $(SUBDIRS); do \
	 $(MAKE) -C $$i O=$(O) all;\
	done

install:
	for i in $(SUBDIRS); do \
	  $(MAKE) -C $$i O=$(O) DESTDIR=$(DESTDIR) install; \
	done


clean:
	for i in $(SUBDIRS); do \
	 $(MAKE) -C $$i O=$(O) clean; \
	done



# End File.


