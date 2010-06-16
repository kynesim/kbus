#! /usr/bin/env python
"""Run a Python interpreter and output its result as reStructuredText.

It attempts to output the result in a form that can be cut-and-pasted
straight into the reStructuredText document.

Note that just about everything about this script is exceedingly hacky...

Use -debug to see the indentation explicitly.
"""

import os
import fcntl
import errno
import signal
import sys
import subprocess
import time

DEFAULT_WAIT = 0.1
DEFAULT_INTRO = """\
  .. compound::

     *Terminal %d: %s* ::

"""
DEFAULT_INDENT  = "       "

# The various ways people commonly represent control-c
CONTROL_C_STRINGS = ("^C", "<CTRL-C>", "<CONTROL-C>", "<CTRL_C>", "<CONTROL_C>")

class Terminal(object):
    """This class represents one "terminal" in our example.

    Or, putting it another way, one of the "Actors" in our play...

    A terminal has a name (the actor name) and an index (the terminal number).
    """

    def __init__(self, index, name, *args, **kwargs):
        # Certainly on Linux (Ubuntu) things don't seem to work
        # properly if we use "shell=True".
        self.interp = subprocess.Popen(['python', '-u', '-i', '-'],
                                       stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT,
                                       )
        self.name = name
        self.index = index

        self._debug = False

        # Make the process's standard output non-blocking
        fl = fcntl.fcntl(self.interp.stdout, fcntl.F_GETFL)
        fcntl.fcntl(self.interp.stdout, fcntl.F_SETFL, fl | os.O_NONBLOCK)

        # Add the interpreter announcement text to the first prompt
        text, prompt = self._read()
        self.last_prompt = '%s\n%s'%(text,prompt)

    INTRO = DEFAULT_INTRO
    INDENT  = DEFAULT_INDENT

    def debug(self, on=True):
        if on:
            self.INDENT = "~~~~~~~"
        else:
            self.INDENT = DEFAULT_INDENT

    def _read(self, wait=DEFAULT_WAIT):
        if wait:
            time.sleep(wait)
        try:
            self.interp.stdin.flush()
            stuff = self.interp.stdout.read()
            lines = stuff.split('\n')
            if lines[-1] in ('>>> ', '... ', '>>> >>> '):
                # It's our next prompt
                prompt = lines.pop(-1)
                if prompt == '>>> >>> ':
                    # This seems to happen after CTRL-C?
                    prompt = '>>> '
            else:
                prompt = None
            if self._debug:
                print lines, prompt
            return '\n'.join(lines), prompt
        except IOError as exc:
            if exc.errno == errno.EAGAIN:
                if self._debug:
                    print '<nothing to read>'
                return None, None
            else:
                raise

    def _control_c(self, wait=DEFAULT_WAIT):
        time.sleep(wait)
        # Don't try to read anything, as we assume that we're
        # trying to break out of (for instance) an infinite loop
        self._write_indented('<CTRL-C>')
        self.interp.send_signal(signal.SIGINT)
        text, prompt = self._read(wait)
        self._write('\n')
        return text, prompt

    def _write(self, text):
        sys.stdout.write(text)

    def _write_indented(self, text):
        if text:
            lines = text.split('\n')
            new = []
            for line in lines:
                new.append('%s%s'%(self.INDENT, line))
            sys.stdout.write('\n'.join(new))

    def do(self, *lines, **kwargs):
        """

        * 'wait' is seconds to wait before each read

        The string <CONTROL-C> (or any of the allowed variants,
        see the CONTROL_C_STRINGS value) is treated as an instruction
        to send a control-c (SIGINT) to the interpreter.
        """
        wait = kwargs.get('wait', DEFAULT_WAIT)

        prompt = None
        text = None
        self._write(self.INTRO%(self.index, self.name))
        if self.last_prompt:
            self._write_indented(self.last_prompt)
            self.last_prompt = None
        for line in lines:
            if text:
                self._write('\n')
            if prompt:
                self._write_indented(prompt)
            if line in CONTROL_C_STRINGS:
                text, prompt = self._control_c()
            else:
                self.interp.stdin.write('%s\n'%line)
                self.interp.stdin.flush()
                self._write('%s\n'%line)
                text, prompt = self._read(wait)
            self._write_indented(text)
        if prompt:
            self.last_prompt = prompt
        if text:
            self._write('\n')

    def show(self, wait=DEFAULT_WAIT):
        """Call when there's output but no input or prompt.
        """
        text = None
        self._write(self.INTRO%(self.index, self.name))
        text, prompt = self._read(wait)
        self._write_indented(text)
        if prompt:
            self.last_prompt = prompt


def main(args):
    if args and args[0] in ('-help', '--help', '-h'):
        print __doc__
        return

    if args and args[0] == '-debug':
        debugging = True
    else:
        debugging = False

    x = Terminal(0, "Test")
    if debugging:
        x.debug()

    print
    x.do('import os')
    print
    x.do('print dir(os)[:4]')
    print
    x.do("import time",
         "while 1:",
         "   time.sleep(1)",
         "")
    print
    x.do("<CTRL-C>",
         "import sys",
         "print sys.argv")
    print
    x.do(r"print '12345\n67890'",
         "print 'Bye'",
         "exit()")

if __name__ == '__main__':
    main(sys.argv[1:])
    print

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
