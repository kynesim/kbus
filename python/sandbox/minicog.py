#! /usr/bin/env python

"""A (very much) more primitive equivalent of Ned Batchelder's cog

(by the way, cog itself is really neat and you should check it out)

Usage: minicog.py <infile> [<outfile>]

If <outfile> is '-' then it stdout will be used.

We're more primitive because we don't do many of the things cog does,
especially handling non-reStructuredText markup, retaining the original
code, and being able to write (back) to the original file.

On the other hand, our markup is just::

    .. [[[
    <code to run>
    .. ]]]

Indentation within a single "block" must be consistent with the first
line. All lines in the "block" must start with at least 3 spaces, and
also at least the same number of spaces as the first line of the "block".
"""

import sys
import os
import StringIO

outputter = StringIO.StringIO()

def canonical_path(path):
    path = os.path.expandvars(path)       # $NAME or ${NAME}
    path = os.path.expanduser(path)       # ~
    path = os.path.normpath(path)
    path = os.path.abspath(path)          # copes with ., thing/../fred, etc.
    return path

def next_line(infd):
    line = infd.readline()
    if line:
        return line
    else:
        raise StopIteration

def calc_indent(line):
    indent_len = 0
    while line[indent_len] == ' ':
        indent_len += 1
    return indent_len

def indent_error(expected_indent, line):
    print 'Stopping - line appears wrongly indented'
    print 'Expecting indentation of %d, got %d'%(expected_indent,calc_indent(line))
    print '"%s"'%line.rstrip()
    raise StopIteration

def get_next_generator(infd, globals):
    while True:
        line = next_line(infd)
        if line.startswith('.. [[['):
            indent_len = indent_str = None
            codelines = []
            line = next_line(infd)
            while not line.startswith('.. ]]]'):
                if not line.strip():
                    # Empty line - just accept it as is
                    # (do we care? should we really just ignore it?)
                    codelines.append('')
                elif line.startswith('   '):    # minimum for an rst comment
                    if indent_str:
                        if not line.startswith(indent_str):
                            indent_error(indent_len, line)
                    else:
                        indent_len = calc_indent(line)
                        indent_str = indent_len * ' '
                    codelines.append(line[indent_len:].rstrip())
                else:
                    indent_error(3, line)
                line = next_line(infd)
            codelines.append('')
            try:
                sys.stdout = outputter
                exec '\n'.join(codelines) in globals
            finally:
                sys.stdout = sys.__stdout__
            result = outputter.getvalue()
            outputter.truncate(0)
            yield result
        else:
            yield line

def process(infd, outfd):
    get_next = get_next_generator(infd, {})
    for text in get_next:
        outfd.write(text)

def main(args):

    if len(args) == 0 or args[0] in ('-help', '--help', '-h'):
        print __doc__
        return

    if len(args) == 2:
        infile = args[0]
        outfile = args[1]
    elif len(args) == 1:
        infile = args[0]
        outfile = '%s.tmp'%os.path.splitext(infile)[0]
    else:
        print __doc__
        return

    if outfile != '-' and canonical_path(infile) == canonical_path(outfile):
        print "Input and output are the same (%s) - shan't."%canonical_path(infile)
        return

    print 'Input: ',infile
    print 'Output:',outfile if outfile != '-' else '<stdout>'

    with open(infile) as infd:
        if outfile == '-':
            process(infd, sys.stdout)
        else:
            with open(outfile,'w') as outfd:
                process(infd, outfd)

if __name__ == '__main__':
    main(sys.argv[1:])

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
