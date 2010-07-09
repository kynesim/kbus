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
#   Tibs <tibs@tonyibbs.co.uk>
#
# ***** END LICENSE BLOCK *****

RULES_NAME = 45-kbus.rules

ifneq ($(KERNELRELEASE),)
	# We are being invoked from inside a kernel build
	# so can just ask it to build us
	obj-m = kbus.o
else
	# We are being invoked by make directly
	# We need to ask the kernel build system to do its
	# thing
	#
	# If we haven't been given a value for KERNELDIR, or if we've been
	# given an empty value, then guess the kernel build system is in the
	# normal place (depending on what our system is)
ifeq ($(strip $(KERNELDIR)),)
	KERNELDIR = /lib/modules/$(shell uname -r)/build
endif

	PWD = $(shell pwd)
	KREL_DIR = modules/$(shell uname -r)


# For kbus global builds - build everything here, then move the target
# out of the way and clean up. Turns out that the Kernel makefile
# really doesn't like building object files in non-source directories,

all: kbus.ko $(RULES_NAME)

# We use 'O= ' deliberately, because kernel make, which creates the .ko
# does not like to build object files in non-source directories.
kbus.ko :
	$(MAKE) -C $(KERNELDIR) M=$(PWD) O= modules
	
# The mechanism is a bit hacky (!) - first we make sure we've got a local
# copy of the file we want, then we copy it into place
#
# Just to make life more fun, in Ubuntu 9.10, the file has to be in
# /lib/udev/rules.d - putting it in the previous location doesn't seem
# to do anything (at least on a fresh install of 9.10). It *does*, however
# appear to be enough to link one to the other...

# On Ubuntu, if we want ordinary users (in the admin group) to be able to
# read/write '/dev/kbus<n>' then we need to have a rules file to say so.
# This target is provided as a convenience in this matter.
RULES_FILE = "/etc/udev/rules.d/$(RULES_NAME)"
RULES_LINE = "KERNEL==\"kbus[0-9]*\",  MODE=\"0666\", GROUP=\"admin\""

$(RULES_NAME) : 
	@echo $(RULES_LINE) > $(RULES_NAME)

rules: $(RULES_NAME)
	@ if [ -e $(RULES_FILE) ]; \
	then echo $(RULES_FILE) already exists ; \
	else cp $(RULES_NAME) $(RULES_FILE) ; \
	fi
	@ if [ -d /lib/udev/rules.d ]; \
	then ln -sf $(RULES_FILE) /lib/udev/rules.d/; \
	fi

# Install the header file first, since we have a user system that sometimes
# wants to install the header file, but fails to build the kernel module,
# and we're feeling friendly.
install:
	-mkdir -p $(DESTDIR)/include/kbus
	install -m 0644 kbus_defns.h $(DESTDIR)/include/kbus/kbus_defns.h
	$(MAKE) -C $(KERNELDIR) M=$(PWD) O= modules_install

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
