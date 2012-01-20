#! /usr/bin/env python

"""Copy the KBUS kernel module sources into the Linux kernel source tree

After this, it should be possible to build the KBUS kernel module as part
of the normal Linux build.

Usage:

    copy_into_kernel.py [<switches>] <kernel-module-dir> <linux-kernel-root>

where:

    * <switches> may be any of:

        -n               - by default don't build the KBUS kernel module
        -m               - by default build it as a module
        -y               - by default build it in
        -from <KBUS-dir> - specify a different source directory

      The -n, -m and -y switches change the default set in the KBUS KConfig
      file. If none is specified, then the file will not be edited - that is,
      if it is copied, the value from the copied file will be retained, and
      if KBUS configuration was already present, it will not be changed.

    * <linux-kernel-root> is the directory at the root of the Linux
      kernel source tree (so the directory containing the Documentation/
      directory and the COPYING file).

By default, the KBUS kernel module sources are copied from the directory
containing this script. Use the -from switch to change this.
"""

import os
import shutil
import sys

THIS_DIR  = os.path.split(__file__)[0]

class GiveUp(Exception):
    pass

def expand_path(path):
    path = os.path.expandvars(path)     # expand $(WHATEVER)
    path = os.path.expanduser(path)     # expand ~
    path = os.path.normpath(path)       # remove //, etc.
    return path

def main(args):
    if not args:
        raise GiveUp(__doc__)

    edit_kbus_kconfig = False
    default_action = 'y'
    kbus_dir = THIS_DIR

    while args and args[0][0] == '-':
        word = args[0]
        if word in ('-h', '--help', '-help'):
            print __doc__
            return
        elif word == '-n':
            default_action = 'n'
            edit_kbus_kconfig = True
        elif word == '-m':
            default_action = 'm'
            edit_kbus_kconfig = True
        elif word == '-y':
            default_action = 'y'
            edit_kbus_kconfig = True
        elif word == '-from':
            if not args:
                raise GiveUp("'-from' switch needs a directory as argument")
            kbus_dir = expand_path(args[0])
            args = args[1:]
        else:
            raise GiveUp("Unexpected switch '%s'. Try '-help' for help."%word)
        args = args[1:]

    if not args:
        raise GiveUp("The location of the kernel source tree must be specified. Try '-help' for help.")
    if len(args) != 1:
        raise GiveUp("Too many arguments. Try '-help' for help.")

    linux_dir = expand_path(args[0])

    print 'Copying KBUS kernel module'
    print 'From:', kbus_dir
    print 'To:  ', linux_dir

    ipc_dir = os.path.join(linux_dir, 'ipc')

    print 'Copying source files to', ipc_dir
    print '  kbus_main.c'
    shutil.copy2(os.path.join(kbus_dir, 'kbus_main.c'), ipc_dir)
    print '  kbus_report.c'
    shutil.copy2(os.path.join(kbus_dir, 'kbus_report.c'), ipc_dir)
    print '  kbus_internal.h'
    shutil.copy2(os.path.join(kbus_dir, 'kbus_internal.h'), ipc_dir)

    linux_hdr_dir = os.path.join(linux_dir, 'include', 'linux')
    print 'Copying kbus_defns.h to', linux_hdr_dir
    shutil.copy2(os.path.join(kbus_dir, 'linux', 'kbus_defns.h'), linux_hdr_dir)

    # It's friendly to copy the documentation as well
    linux_doc_dir = os.path.join(linux_dir, 'Documentation')
    print 'Copying Kbus.txt to', linux_doc_dir
    shutil.copy2(os.path.join(kbus_dir, 'other', 'Documentation', 'Kbus.txt'), linux_doc_dir)

    # We need to add our own specs to the end of the 'ipc' Makefile,
    # if they're not already there
    ipc_makefile = os.path.join(ipc_dir, 'Makefile')
    print 'Looking at', ipc_makefile
    with open(ipc_makefile) as fd:
        lines = fd.readlines()
    found_kbus = False
    for line in lines:
        if 'CONFIG_KBUS' in line:
            found_kbus = True
            break
    if found_kbus:
        print '  KBUS appears to be there already'
        print '    "%s"'%line.strip()
    else:
        print '  Adding KBUS lines to the end'
        with open(os.path.join(kbus_dir, 'other', 'ipc', 'Makefile_kbus_lines')) as fd:
            extra_lines = fd.readlines()
        lines.append('\n')
        for line in extra_lines:
            lines.append(line)
        with open(ipc_makefile, 'w') as fd:
            fd.writelines(lines)

    # Kconfig is a bit more complicated
    ipc_kconfig = os.path.join(ipc_dir, 'Kconfig')
    kbus_kconfig = os.path.join(kbus_dir, 'other', 'ipc', 'Kconfig')
    print 'Looking at', ipc_kconfig
    if os.path.exists(ipc_kconfig):
        print '  The IPC Kconfig file already exists'
        with open(ipc_makefile) as fd:
            lines = fd.readlines()
        found_kbus = False
        for line in lines:
            if 'KBUS' in line:
                found_kbus = True
                break
        if found_kbus:
            print '  KBUS appears to be there already'
        else:
            print '  KBUS does not appear to be there yet - adding KBUS lines to the end'
            with open(kbus_kconfig) as fd:
                extra_lines = fd.readlines()
            lines.append('\n')
            for line in extra_lines:
                lines.append(line)
            with open(ipc_makefile, 'w') as fd:
                fd.writelines(lines)
    else:
        print '  The IPC Kconfig file does not exist yet'
        print '  Copying the KBUS IPC Kconfig file'
        shutil.copy2(kbus_kconfig, ipc_kconfig)

    print 'Checking the KBUS default in', ipc_kconfig
    with open(ipc_kconfig) as fd:
        lines = fd.readlines()
    found_line = None
    for count, line in enumerate(lines):
        if line.strip().startswith('config KBUS'):
            if lines[count+1].strip().startswith('tristate') and \
               lines[count+2].strip().startswith('default'):
                   found_line = count+2
                   break
    if found_line is None:
        raise GiveUp("Cannot find KBUS configuration in %s"%ipc_kconfig)

    default_line = lines[found_line]
    words = default_line.split()
    state = words[1].lower()

    if edit_kbus_kconfig:
        if state == default_action:
            print "  KBUS defaults to '%s', which matches the requested state"%state
        else:
            print "  KBUS was defaulting to '%s', changing it to '%s'"%(state, default_action)
            leader = ''
            for ch in default_line:
                if ch in (' ', '\t'):
                    leader += ch
                else:
                    break
            lines[found_line] = '%sdefault %s\n'%(leader, default_action)
            with open(ipc_kconfig, 'w') as fd:
                fd.writelines(lines)
    else:
        print "  KBUS defaults to '%s'"%state

    # We probably need to edit the 'init' Kconfig file
    init_kconfig = os.path.join(linux_dir, 'init', 'Kconfig')
    print 'Looking at', init_kconfig
    with open(init_kconfig) as fd:
        lines = fd.readlines()
    found_ipc_kconfig = False
    for line in lines:
        if 'ipc/Kconfig' in line:
            # That's probably the right line, but let's be more specific
            words = line.split(line)
            if words[0] == 'source' and words[1] == 'ipc/Kconfig':
                found_ipc_kconfig = True
                break

    if found_ipc_kconfig:
        print '  It already sources the IPC Kconfig'
    else:
        print '  Adding \'source "ipc/Kconfig"\' to the end'
        # Given it's just for KBUS, we'll add it to the end, since that
        # is simplest to do and simplest to find when configuring
        lines.append('\n')
        lines.append('source "ipc/Kconfig"\n')
        with open(init_kconfig, 'w') as fd:
            fd.writelines(lines)

    # And that's all...
    print 'Done'

if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except GiveUp as e:
        print e
        sys.exit(1)

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
