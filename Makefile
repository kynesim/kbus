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
#   Tibs <tony.ibbs@gmail.com>
#
# ***** END LICENSE BLOCK *****

LIBKBUSDIR=libkbus



# If set to 1, we become extremely verbose. If set to 0, we don't.
#
# Whilst our debugging messages are output as KERN_DEBUG, this can
# still be a pain if other modules want to use KERN_DEBUG
#
ifeq ($(VERBOSE_DEBUG),)
EXTRA_CFLAGS=-DVERBOSE_DEBUG=0
else
EXTRA_CFLAGS=-DVERBOSE_DEBUG=$(VERBOSE_DEBUG)
endif

ifneq ($(KERNELRELEASE),)
	# We are being invoked from inside a kernel build
	# so can just ask it to build us
	obj-m = kbus.o
else
	# We are being invoked by make directly
	# We need to ask the kernel build system to do its
	# thing
	#
	# Unless we've been given a value for KERNELDIR, guess the kernel build
	# system is in the normal place (depending on what our system is)
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build

	PWD = $(shell pwd)
	KREL_DIR = modules/$(shell uname -r)


# Build the KBUS kernel module.
# Copy it to a directory named after the kernel version used to so build
# - this allows someone building multiple different versions of the module
#   to have some hope of keeping track
# We use a "modules/<uname -r>" because that mirrors /lib/modules/ in the
# "real" Linux layout
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) O= modules
	mkdir -p $(KREL_DIR)
	cp kbus.ko $(KREL_DIR)


# For kbus global builds - build everything here, then move the target
# out of the way and clean up. Turns out that the Kernel makefile
# really doesn't like building object files in non-source directories,
all: 
	rm -f kbus.mod.c *.o kbus.ko .kbus*.cmd Module.* modules.order 
	rm -rf .tmp_versions
	$(MAKE) -C $(KERNELDIR) M=$(PWD) O= modules
	-mkdir -p $(O)/kbus
	mv kbus.ko $(O)/kbus


# On Ubuntu, if we want ordinary users (in the admin group) to be able to
# read/write '/dev/kbus<n>' then we need to have a rules file to say so.
# This target is provided as a convenience in this matter.
RULES_NAME = 45-kbus.rules
RULES_FILE = "/etc/udev/rules.d/$(RULES_NAME)"
RULES_LINE = "KERNEL==\"kbus[0-9]*\",  MODE=\"0666\", GROUP=\"admin\""
# The mechanism is a bit hacky (!) - first we make sure we've got a local
# copy of the file we want, then we copy it into place
rules:
	@ if [ ! -e $(RULES_NAME) ]; \
	then echo $(RULES_LINE) > $(RULES_NAME); \
	fi
	@ if [ -e $(RULES_FILE) ]; \
	then echo $(RULES_FILE) already exists ; \
	else sudo cp $(RULES_NAME) $(RULES_FILE) ; \
	fi

install:
	-mkdir -p $(DESTDIR)/kmodules
	install -m 0755 $(O)/kbus/kbus.ko $(DESTDIR)/kmodules/kbus.ko
	-mkdir -p $(DESTDIR)/include/kbus
	install -m 0644 kbus_defns.h $(DESTDIR)/include/kbus/kbus_defns.h
	-mkdir -p $(DESTDIR)/etc/udev/rules.d
	echo $(RULES_LINE) >$(DESTDIR)/etc/udev/rules.d/$(RULES_NAME)

# Only remove "modules" if we're doing a bigger clean, as there might
# be subdirectories from previous builds that we don't want to lose on
# a normal clean
distclean:
	rm -rf modules

clean:
	rm -f kbus.mod.c *.o kbus.ko .kbus*.cmd Module.* modules.order 
	rm -rf .tmp_versions
	rm -rf *.pyc $(RULES_NAME)
endif
