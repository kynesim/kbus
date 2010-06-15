#! /usr/bin/env python
"""Generate reStructuredText for my EuroPython2010 talk.
"""

from rst_terminal import Terminal

print "Introducing Rosencrantz"
r = Terminal(1, "Rosencrantz")
r.do("from kbus import Ksock",
     "rosencrantz = Ksock(0)",
     "print rosencrantz")

print
print "Rosencrantz can send a message."
r.do("from kbus import Message",
     "ahem = Message('$.Actor.Speak', 'Ahem')",
     "rosencrantz.send_msg(ahem)")

print
print "But no-one is listening."
print
print "    (explain about message ids, though)"
print
print "Introduce the audience, who bind to the message Rosencrantz was using."
a = Terminal(2, "Audience")
a.do("from kbus import *",
     "audience = Ksock(0)",
     "audience.bind('$.Actor.Speak')")

print
print "If Rosencrantz sends the message again, the audience can receive it."
print
print "    (note the new message id)"
r.do("rosencrantz.send_msg(ahem)")
a.do("audience.read_next_msg()")

print
print "Or, more prettily"
print
print "    (note how the message id matches that given to Rosencrantz)"
a.do("print _")

print
print "We can set the audience to listening using 'select'"
a.do("import select",
      "while 1:",
      "   (r,w,x) = select.select([audience], [], [])",
      "   # At this point, r should contain audience",
      "   print audience.read_next_msg()",
      "")

print
print "So now if Rosencrantz talks..."
r.do("rosencrantz.send_msg(Message('$.Actor.Speak', 'Hello there'))",
     "rosencrantz.send_msg(Message('$.Actor.Speak', 'Can you hear me?'))")

print
print "...the audience should be able to hear him:"
a.show()

print
print "Introducing Guildenstern."
g = Terminal(3, "Guildenstern")
g.do("from kbus import *",
     "guildenstern = Ksock(0)",
     "print guildenstern")

print
print "Who also starts listening - this time using a wildcard"
g.do("guildenstern.bind('$.Actor.*')")

print
print "In retrospect this makes sense for the audience, too - let's fix that"
print "(and use the KBUS provided way of doing our 'select' loop as well)"

a.do("<CONTROL_C>",
    "audience.bind('$.Actor.*')",
     "while 1:",
     "   print audience.wait_for_msg()",
     "")

print
print "There's nothing for Guildenstern to hear yet, of course."
g.do("print guildenstern.read_next_msg()")

print
print "Maybe rosencrantz will want to hear his colleague:"
r.do("rosencrantz.bind('$.Actor.*')")

print
print "So let guildenstern speak:"
g.do("guildenstern.send_msg(Message('$.Actor.Speak', 'Pssst!'))",
     "# Remember guildenstern is himself listening to '$.Actor.*'",
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
print "The solution is simple - ask not to hear the more specific version"
print "(an unbinding must match the binding exactly)."

a.do("<CONTROL-C>",
     "audience.unbind('$.Actor.Speak')",
     "while 1:",
     "   msg = audience.wait_for_msg()",
     "   print msg",
     "")

print """
Note that messages also say who they are from.
Each Ksock connection has an id associated with it - for instance:"""

r.do("rosencrantz.ksock_id()")

print
print "and every message indicates who sent it, so:"

r.do("print msg")

print """
We've shown that KBUS allows one to "announce" (or, less politely,
"shout") messages, but KBUS also supports asking questions.

So let's make Guildenstern listen to "Speak" messages, and act as a
Replier for "query" messages..."""

g.do("guildenstern.unbind('$.Actor.*')",
     "guildenstern.bind('$.Actor.Speak')",
     "guildenstern.bind('$.Actor.Guildenstern.Query', True)")

print """
   *(Only one person may be bound as Replier for a particular message
   name at any one time, so that it is unambiguous who is expected to do
   the replying.*

   *Also, if a Sender tries to send a Request, but no-one has bound to that
   message name as a Replier, then an error is raised (contrast that with
   ordinary messages, where if no-one is listening, the message just gets
   ignored).)*

If Rosencrantz then sends a Request of that name:"""

r.do("from kbus import Request",
     "req = Request('$.Actor.Guildenstern.Query', 'Were you speaking to me?')",
     "rosencrantz.send_msg(req)")

print
print "Remember, Rosencrantz still hears himself speaking - so"
print "let's undo that..."
r.do("print rosencrantz.read_next_msg()",
     "rosencrantz.unbind('$.Actor.*')")

print "\nGuildenstern can receive it:"

g.do("msg2 = guildenstern.read_next_msg()",
     "print msg2",
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

print
print "(and that is what the ``YOU`` in the flags means)."
print
print "Guildenstern can then reply:"

g.do("reply = reply_to(msg2, 'Yes, yes I was')",
     "print reply",
     "guildenstern.send_msg(reply)",
     "guildenstern.read_next_msg()")

print """
The ``reply_to`` convenience function crafts a new ``Reply`` message, with the
various message parts set in an appropriate manner. And thus:"""

r.do("rep = rosencrantz.read_next_msg()",
     "print rep")

print """
Note that Rosencrantz didn't need to be bound to this message to receive it -
he will always get a Reply to any Request he sends (KBUS goes to some lengths
to guarantee this, so that even if Guildenstern closes his Ksock, it will
generate a "gone away" message for him)."""

print
print "Of course, the audience was listening."
a.show()

# ============================================================================
print
print "And to end things..."
a.do("<CONTROL_C>",
     "exit()")
print
print "Tidy everyone else up as well (note iterating over messages)"
r.do("for msg in rosencrantz:",
     "    print msg",
     "",
     "exit()")
g.do("for msg in guildenstern:",
     "    print msg",
     "",
     "exit()")

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
