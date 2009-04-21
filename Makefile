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

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
	$(MAKE) -C $(LIBKBUSDIR)

clean:
	rm -f kbus.mod.c *.o kbus.ko .kbus*.cmd Module.* modules.order 
	rm -rf .tmp_versions
	$(MAKE) -C $(LIBKBUSDIR) clean
endif
