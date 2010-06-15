#! /usr/bin/env python
"""Run the examples for the simple introduction to KBUS, more-or-less
slides/04.kbus

Not all of the intermediate text is here.

Some of the examples needed fixing a bit, and there's a little bit
extra at the end.
"""

from rst_terminal import Terminal

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

g.do("reply = reply_to(msg2, 'Yes, yes I was')",
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
a.control_c()
a.do("exit()")

print "\nTidy everyone else up as well (note iterating over messages)"

r.do("for msg in rosencrantz:",
     "    print msg",
     "",
     "exit()")

g.do("for msg in guildenstern:",
     "    print msg",
     "",
     "exit()")

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
