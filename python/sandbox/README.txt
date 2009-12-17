=============
KBUS workings
=============

Initial musings
---------------

The current task is to take KBUS-the-single-plaform and add the ability to do
platform-to-platform communication.

At the moment, we have a kernel module which allows one to do::

      ..

                +---------------------------------------+
                |                                       |
      sender -->|                                       |---> listener
                |                                       |
                +---------------------------------------+

(and back again). What we want is to have something that allows us to do::

      ..

                +--------------+     +-----------------+
                |             /     /                  |
      sender -->|             \ --> \                  |---> listener
                |             /     /                  |
                +------------+     +-------------------+

where the two halves each "look like" the previous mechanism, but we can put
different sorts of glue in between - including peer-to-peer communications
between two boards.

My proposed way to get there is:

1. Implement a KBUS "look alike" in Python - i.e,, a daemon that does what the
   kernel module does, but in user space.

   This is in Python because it will be faster to implement.

   It should be able to pass all of the relevant unit tests (which will also
   mean pulling the unit tests apart into appropriate subsets: those which are
   generic, those which relate to the kernel module only, including setup and
   teardown, and those which relate to the Python daemon only).

2. Tease apart the innards of the Python daemon so it can be two comminicating
   daemons, one for each "end" of a KBUS. Initially, this can be two daemons
   on the same machine.

3. Make it work across two machines (via whatever transport mechanism is most
   convenient).

4. Translate it to C - although I don't see why the API for using it should be
   any different, but we don't really want to have to have Python on the
   machine at either end.

Thinking on actual work
-----------------------

   ...which doesn't necessarily match what was talked about above...

Consider a particular machine as a goldfish bowl. Inside is a KBUS kernel
module, and the contents of the bowl communicate with this (and thus each
other) in the normal KBUS manner.

Now consider another goldfish bowl. We'd like to be able to make the two KBUS
kernel modles (one in each) communicate.

So, let's place a limpet on the inside of each bowl's glass. Each limpet can
communicate with the other using a simple laser communications link (so
they're clever cyborg limpets), and each limpet can also communicate with its
KBUS kernel module.

::

        ..

           +          +                 +          +   
          /           |\               /|           \  
         /      +.....|L\ ~ ~ ~ ~ ~ ~ /L|....+       \ 
        +       :     +--+           +--+    :        +
        |       :        |           |       :        |
        +       :        +           +       :        +
         \    +---+     /             \    +---+     / 
          \   | K |    /               \   | K |    /  
           +--+---+---+                 +--+---+---+   
              Bowl A                       Bowl B

So the Limpet needs to proxy things for KBUS users in its bowl to the other
bowl, and back again.

So:

* if Someone in Bowl A (let's call them S'a) wants to bind as a listener to
  message M'1, then we want the Limpet in Bowl B to forward all M'1 messages
  from Bowl B, and the Limpet in Bowl A then needs to echo them to the KBUS
  kernel module in Bowl A::

        ..

           +          +                 +          +   
          /           |\               /|           \  
         /      +.....|L\ < < < < < < /L|<...+       \ 
        +       :     +--+           +--+    :        +
        |       :        |           |       :        |
        +       v        +           +       :        +
         \    +---+     /             \    +---+     / 
          \   | K |    /               \   | K |    /  
           +--+---+---+                 +--+---+---+   
              Bowl A                       Bowl B

* what if S'A wants to bind as a replier for message Q'1?

  They do so in the normal manner. However, in order for someone in Bowl B to
  be able to send requests from their bowl to the other, the Limpet in Bowl B
  needs to bind as a (proxy) replier for Q'1.

  The sequence would then be:

  * S'B can send a request, Q'1
  * The KBUS kernel module in Bowl B knows there is a replier bound for this -
    it is the Bowl B Limper. So it send the request to it.
  * The Bowl B Limpet forwards the requset to the Bowl A Limpet.
  * The Bowl A Limpet acts as a proxy sender for Q'1 - it sends Q'1 to the
    Bowl A KBUS kernel module.
  * The Bowl A KBUS kernel module sends the request to S'A, who is the actual
    replier.

  And one can construct a similar path in reverse, for the eventual reply.

To make this simple (!), *someone* needs to tell the Limpets what to do -
i.e., which messages to act as proxy-repliers for, and which to act as
proxy-listeners for.

One can't do the proxy replier part automatically, at least at the moment,
because we have no way of detecting when someone has bound as a replier.

  So perhaps the first thing needed is to have a mechanism to tell when this
  happens - obviously, by broadcasting a message (!) to say "A has bound to
  message name M as a replier". If we allowed these to propagate between
  bowls, then we could allow the Limpets to propagate the bindings. And, of
  course, we'd need a similar message for unbinding (as a replier) - whether
  by actually using ``unbind``, or by "going away".

Separately, we presumably don't want to share *all* messages between the two
bowls. So it would seem reasonable to have some way of telling the Limpet in
one's own bowl what sorts of message to propagate (or, perhaps, what not to
propagate).

.. vim: set filetype=rst tabstop=8 shiftwidth=2 expandtab:
