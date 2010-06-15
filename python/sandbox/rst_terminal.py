#! /usr/bin/env python
"""Run a Python interpreter and output its result as reStructuredText.

It attempts to output the result in a form that can be cut-and-pasted
straight into the reStructuredText document.

Note that just about everything about this script is exceedingly hacky...
"""

import os
import fcntl
import errno
import signal
import sys
import subprocess
import time

INTRO = """
  .. compound::

     *Terminal %d: %s* ::

"""

INDENT  = "       "
DEFAULT_WAIT = 0.1

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
        text, prompt = self.read()
        self.last_prompt = '%s\n%s'%(text,prompt)

    def read(self, wait=DEFAULT_WAIT):
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

    def _write(self, text):
        sys.stdout.write(text)

    def _write_indented(self, text):
        if text:
            lines = text.split('\n')
            new = []
            for line in lines:
                new.append('%s%s'%(INDENT, line))
            sys.stdout.write('\n'.join(new))

    def do(self, *lines, **kwargs):
        """

        * 'wait' is seconds to wait before each read
        * 'prompt' (default True) indicates if we're expecting the
          interpreter to output a prompt. This should be False if we are,
          for instance, just expecting output from some sort or ongoing loop.
        """
        wait = kwargs.get('wait', DEFAULT_WAIT)

        prompt = None
        text = None
        self._write(INTRO%(self.index, self.name))
        if self.last_prompt:
            self._write_indented(self.last_prompt)
            self.last_prompt = None
        for line in lines:
            if text:
                self._write('\n')
            if prompt:
                self._write_indented(prompt)
            self.interp.stdin.write('%s\n'%line)
            self.interp.stdin.flush()
            self._write('%s\n'%line)
            text, prompt = self.read(wait)
            self._write_indented(text)
        if prompt:
            self.last_prompt = prompt
        if text:
            self._write('\n')

    def show(self, wait=DEFAULT_WAIT):
        """Call when there's output but no input or prompt.
        """
        text = None
        self._write(INTRO%(self.index, self.name))
        text, prompt = self.read(wait)
        self._write_indented(text)
        if prompt:
            self.last_prompt = prompt

    def control_c(self, wait=DEFAULT_WAIT):
        self._write(INTRO%(self.index, self.name))
        time.sleep(wait)
        # Don't try to read anything, as we assume that we're
        # trying to break out of (for instance) an infinite loop
        self._write_indented('<CONTROL-C>')
        self._write('\n')
        self.interp.send_signal(signal.SIGINT)
        text, prompt = self.read(wait)
        self._write_indented(text)
        if prompt:
            self.last_prompt = prompt
        self._write('\n')


def main():
    x = Terminal(0, "Test")
    x.do('import os')
    x.do('print dir(os)[:4]')
    x.do("import time",
         "while 1:",
         "   time.sleep(1)",
         "")
    x.control_c()
    x.do("import sys",
         "print sys.argv")
    x.do(r"print '12345\n67890'",
         "print 'Bye'",
         "exit()")

if __name__ == '__main__':
    main()
    print

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
