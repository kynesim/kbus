#! /usr/bin/env python
"""Run the examples for the EuroPython talk.

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

def indenter(text):
    lines = text.split('\n')
    indented = []
    for line in lines:
        if line:
            indented.append(INDENT+line)
        else:
            indented.append('')
    return '\n'.join(indented)

class Outputter(object):

    def write(self, text, indent=True):
        if indent:
            sys.__stdout__.write(indenter(text))
        else:
            sys.__stdout__.write(text)

class Terminal(object):
    """This class represents one "terminal" in our example.

    Or, putting it another way, one of the "Actors" in our play...

    A terminal has a name (the actor name) and an index (the terminal number).
    """

    def __init__(self, index, name, *args, **kwargs):
        self.interp = subprocess.Popen('python -u -i -',
                                       shell=True,
                                       stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT,
                                       )
        self.name = name
        self.index = index

        # Make the process's standard output non-blocking
        fl = fcntl.fcntl(self.interp.stdout, fcntl.F_GETFL)
        fcntl.fcntl(self.interp.stdout, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    def read(self, wait=DEFAULT_WAIT):
        if wait:
            time.sleep(wait)
        try:
            self.interp.stdin.flush()
            stuff = self.interp.stdout.read()
            new = []
            lines = stuff.split('\n')
            for line in lines:
                new.append('%s%s'%(INDENT, line))
            lines = '\n'.join(new)
            sys.stdout.write(lines)
            return False
        except IOError as exc:
            if exc.errno == errno.EAGAIN:
                return True
            else:
                raise

    def do(self, *lines, **kwargs):
        """

        * 'wait' is seconds to wait before each read
        * if 'output' (default, True) then we expect output from our
          command(s), otherwise we do not...
        """
        if 'wait' in kwargs:
            wait = kwargs['wait']
        else:
            wait = DEFAULT_WAIT
        if 'output' in kwargs:
            output = kwargs['output']
        else:
            output = True
        sys.stdout.write(INTRO%(self.index, self.name))
        for line in lines:
            if self.read(wait):     # read any left-over output, and the prompt
                sys.stdout.write('%s>>> '%INDENT)
            self.interp.stdin.write('%s\n'%line)
            self.interp.stdin.flush()
            print line
            if output:
                self.read(wait)
        sys.stdout.write('\n')

    def control_c(self, wait=DEFAULT_WAIT):
        time.sleep(wait)
        # Don't try to read anything, as we assume that we're
        # trying to break out of (for instance) an infinite loop
        sys.stdout.write('%s<CONTROL-C>\n'%INDENT)
        self.interp.send_signal(signal.SIGINT)
        self.read(wait)
        sys.stdout.write('\n')


def main():

    if 0:
        x = subprocess.Popen('python -u -i -', shell=True, stdin=subprocess.PIPE)
        x.stdin.write('import os\n')
        x.stdin.write('dir(os)\n')
        x.stdin.flush()
        time.sleep(2)
        x.send_signal(signal.SIGINT)
        print
        print 'Next...'

    x = Terminal(0, "Test")
    x.do('import os', ouptut=False)
    x.do('dir(os)')
    x.do("import time",
         "while 1:",
         "   time.sleep(1)",
         "", output=False)
    x.control_c()
    x.read()
    return

    r = Terminal(1, "Rosencrantz")
    r.do("from kbus import Ksock, Message",
         "rosencrantz = Ksock(0)",
         "print rosencrantz",
         "ahem = Message('$.Actor.Speak', 'Ahem')",
         "rosencrantz.send_msg(ahem)")

    a = Terminal(2, "Audience")
    a.do("from kbus import *",
         "audience = Ksock(0)",
         "audience.bind('$.Actor.Speak')")

    g = Terminal(3, "Guildenstern")
    g.do("from kbus import *",
         "guildenstern = Ksock(0)",
         "print guildenstern")

    r.do("rosencrantz.send_msg(ahem)")

    a.do("audience.read_next_msg()")
    a.do("print _")
    a.do("msg = audience.read_next_msg()",
         "print msg")
    a.do("import select",
          "while 1:",
          "   (r,w,x) = select.select([audience], [], [])",
          "   # At this point, r should contain audience",
          "   msg = audience.read_next_msg()",
          "   print 'We heard', msg.name, msg.data",
          "")

    r.do("rosencrantz.send_msg(ahem)")

    a.do("")


if __name__ == '__main__':
    main()
    print

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
