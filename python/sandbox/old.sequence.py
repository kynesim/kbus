#! /usr/bin/env python
"""Run the examples for the EuroPython talk.

It attempts to output the result in a form that can be cut-and-pasted
straight into the reStructuredText document.

Note that the handling of output indentation is nastily hacky!
"""

import sys
from code import InteractiveConsole

INTRO = """
  .. compound::

     *Terminal %d: %s* ::

"""

INDENT  = "       "
PROMPT1 = ">>> %s"
PROMPT2 = "... %s"

def indenter(text):
    lines = text.split('\n')
    indented = []
    for line in lines:
        if line:
            indented.append(INDENT+line)
        else:
            indented.append('')
    return '\n'.join(indented)

class Outputter():

    def write(self, text, indent=True):
        if indent:
            sys.__stdout__.write(indenter(text))
        else:
            sys.__stdout__.write(text)

class Console(InteractiveConsole):

    def write(self, data):
        sys.__stdout__.write(indenter(data))

class Terminal():
    """This class represents one "terminal" in our example.

    Or, putting it another way, one of the "Actors" in our play...

    A terminal has a name (the actor name) and an index (the terminal number).
    """

    def __init__(self, index, name, globals=None):
        self.name = name
        self.index = index
        if globals is None:
            self.globals = {}
        else:
            self.globals = globals

        if "outputter" not in self.globals:
            self.globals["outputter"] = Outputter()

        self.console = Console(locals=self.globals,
                               filename='<%s>'%name)
        self.needsmore = False

        # The following leaves us having "implicitly" imported sys,
        # so far as the user is concerned. Oh well.
        self.console.push("import sys; sys.stdout=outputter")

    def do(self, *lines):
        sys.stdout.write(INTRO%(self.index, self.name), indent=False)
        for line in lines:
            if self.needsmore:
                print PROMPT2%line
            else:
                print PROMPT1%line
            self.needsmore = self.console.push(line)


def main():
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
