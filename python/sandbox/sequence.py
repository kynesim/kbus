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
        self._write_indented('<CONTROL-C>\n')
        self.interp.send_signal(signal.SIGINT)
        text, prompt = self.read(wait)
        self._write_indented(text)
        if prompt:
            self.last_prompt = prompt
        self._write('\n')


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

    if 0:
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
             "print 'Bye'")
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


    r.do("rosencrantz.send_msg(Message('$.Actor.Speak', 'Hello there'))",
         "rosencrantz.send_msg(Message('$.Actor.Speak', 'Can you hear me?'))")

    print
    print "the audience should be able to hear him:"
    a.show()

    print
    print "So now we'll introduce another participant:"
    g = Terminal(3, "Guldenstern")
    g.do("from kbus import *",
         "guildenstern = Ksock(0)",
         "guildenstern.bind('$.Actor.*')")

    print
    print "Here, guildenstern is binding to any message whose name starts with"
    print "``$.Actor.``. In retrospect this, of course, makes sense for the"
    print "audience, too - let's fix that:"

    a.control_c()
    a.do("audience.bind('$.Actor.*')",
         "while 1:",
         "   msg = audience.wait_for_msg()",
         "   print 'We heard', msg.name, msg.data",
         "")

    print
    print "And maybe rosencrantz will want to hear his colleague:"
    r.do("rosencrantz.bind('$.Actor.*')")

    print
    print "So let guildenstern speak:"
    g.do("guildenstern.send_msg(Message('$.Actor.Speak', 'Pssst!'))",
         "# Remember guildenstern is also listening to '$.Actor.*'",
         "print guildenstern.read_next_msg()")

    print
    print "and rosencrantz hears:"
    r.do("msg = rosencrantz.read_next_msg()",
         "print msg")

    print
    print "However, when we look to the audience, we see:"
    a.show()


    print
    print "This is because the audience has bound to the message twice - it is hearing it"
    print "once because it asked to receive every ``$.Actor.Speak`` message, and again"
    print "because it asked to hear any message matching ``$.Actor.*``."
    print
    print "The solution is simple - ask not to hear the more specific version:"

    a.control_c()
    a.do("audience.unbind('$.Actor.Speak')",
         "while 1:",
         "   msg = audience.wait_for_msg()",
         "   print 'We heard', msg.from_, 'say', msg.name, msg.data",
         "")

    print """
Note that we've also amended the printout to say who the message was from.
Each Ksock connection has an id associated with it - for instance:"""

    r.do("rosencrantz.ksock_id()")

    print "\nand every message indicates who sent it, so:"

    r.do("print 'I heard', msg.from_, 'say', msg.name, msg.data")

    print """
We've shown that KBUS allows one to "announce" (or, less politely,
"shout") messages, but KBUS also supports asking questions. Thus:"""

    g.do("guildenstern.bind('$.Actor.Guildenstern.query', True)")

    print """
allows Guildenstern to bind to this new message name as a Replier.

       *(Only one person may be bound as Replier for a particular message
       name at any one time, so that it is unambiguous who is expected to do
       the replying.*

       *Also, if a Sender tries to send a Request, but no-one has bound to that
       message name as a Replier, then an error is raised (contrast that with
       ordinary messages, where if no-one is listening, the message just gets
       ignored).)*

If Rosencrantz then sends a Request of that name:"""

    r.do("from kbus import Request",
         "req = Request('$.Actor.Guildenstern.query', 'Were you speaking to me?')",
         "rosencrantz.send_msg(req)")

    print "\nGuildenstern can receive it:"

    g.do("msg2 = guildenstern.read_next_msg()",
         "print 'I heard', msg2",
         "msg3 = guildenstern.read_next_msg()",
         "print msg3")

    print """
As we should expect, guildenstern is getting the message twice, once because
he has bound as a listener to '$.Actor.*', and once because he is bound as a
Replier to this specific message.

    *(There is, in fact, a way to ask KBUS to only deliver one copy of
    a given message, and if guildenstern had used that, he would only have
    received the Request that was marked for him to answer. I'm still a little
    undecided how often this mechanism should be used, though.)*

Looking at the two messages, the first is the Request specifically to
guildenstern, which he is meant to answer:"""

    g.do("print msg2.wants_us_to_reply()")

    print """
(and that is what the ``YOU`` in the flags means).

And rosencrantz himself will also have received a copy:"""

    r.do("print rosencrantz.read_next_msg()")

    print "\nGuildenstern can then reply:"

    g.do("reply = reply_to(msg2, 'Yes, I was')",
         "print reply",
         "guildenstern.send_msg(reply)")

    print """
The ``reply_to`` convenience function crafts a new ``Reply`` message, with the
various message parts set in an appropriate manner. And thus:"""

    r.do("rep = rosencrantz.read_next_msg()",
         "print 'I heard', rep.from_, 'say', rep.name, rep.data")

    print """
Note that Rosencrantz didn't need to bind to this message to receive it - he
will always get a Reply to any Request he sends (KBUS goes to some lengths to
guarantee this, so that even if Guildenstern closes his Ksock, it will
generate a "gone away" message for him).

And, of course:"""

    a.show()

#    r.do("rosencrantz.send_msg(ahem)")

#    a.show()
#    a.control_c()
#    a.do("print audience.read_next_msg()")


if __name__ == '__main__':
    main()
    print

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
