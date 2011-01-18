#!/usr/bin/env python

from subprocess import *
import os

#############################################################

def cross(new_opts, crosslist):
	"""
	What I want to say is
	  (-Dfoo, -Ufoo) x (-Dbar, -Ubar) x (-Dbaz=1, -Dbaz=42, -Dbaz-69) x ...
	
	So we'll keep an array of arrays-of-args.
	This fn copies the args array appending each arg to it in turn.
	"""
	newlist = [];
	for n in new_opts:
		if len(crosslist)==0:
			t = [n]
			newlist.append(t)
		else:
			for o in crosslist:
				t = list(o) # copy
				t.append(n)
				newlist.append(t)
	return newlist

def cross_ud(sym):
	"""
	Wrapper to cross() which affects the global _tests_ variable
	and adds a -U/-D pair to it.
	"""
	global tests
	tests = cross(['-U%s'%sym, '-D%s'%sym], tests)

def cross_numeric(sym, vals):
	"""
	Wrapper to cross() which affects the global _tests_ variable
	and adds defs for a single symbol with all the given members of
	a LIST of values.
	"""
	global tests
	opts = []
	for v in vals:
		opts.append('-D%s=%d'%(sym,v))
	tests = cross(opts, tests)

tests = []
cross_ud('CONFIG_KBUS_DEBUG')
cross_ud('CONFIG_KBUS_DEBUG_DEBUG_DEFAULT_VERBOSE')
cross_numeric('KBUS_DEBUG_READ',[0,1])
cross_numeric('KBUS_DEBUG_WRITE',[0,1])
cross_numeric('KBUS_DEBUG_REFCOUNT',[0,1])
cross_numeric('KBUS_DEBUG_SHOW_TRANSITIONS',[0,1])

#############################################################

def runtest(testflags, show_all_cases = False, show_output_on_err = True,
		show_output_anyway = False):
	cflags=[]
	cflags.extend(globalCFlags)
	cflags.extend(testflags)
	teststr = ' '.join(cflags)

	p = Popen(["make", "clean"], stdout=PIPE, stderr=STDOUT)
	ret = p.communicate()
	if p.returncode != 0:
		print ret[0]
		raise CalledProcessError("make clean", p.returncode)

	if show_all_cases:
		print "> %s"%teststr

	os.environ['EXTRA_CFLAGS']=teststr
	if show_output_anyway:
		os.environ['VERBOSE']='1'
	else:
		os.environ['VERBOSE']='0'

	p = Popen(['make', 'all'], shell=True,
			stdout=PIPE, stderr=STDOUT)
	ret = p.communicate()
	if p.returncode != 0:
		if show_all_cases:
			print 'FAILED! output follows.'
		else:
			print 'FAILURE: %s'%teststr
		if show_output_on_err:
			print ret[0]
			print "======================================="
		raise CalledProcessError("make", p.returncode)
	else:
		if show_output_anyway:
			print ret[0]
			print "======================================="

globalCFlags=['-DCONFIG_KBUS']

verbose = True
show_errs_detail = True 
continueOnError = True

fails = 0

if verbose:
	print "Running %d tests."%len(tests)
	print "======================================="

for t in tests:
	try:
		# TODO: Make this parallel on multiple CPUs
		runtest(t,verbose,show_errs_detail, False)
	except CalledProcessError:
		fails=fails+1
		if not continueOnError:
			raise

if fails:
	print "%d fails."%fails
	os._exit(1)
else:
	print "All OK"

