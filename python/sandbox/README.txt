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

Testing on one system
---------------------

It should be possible to test on a single system (in one goldfish bowl, with
one kernel module).

Just imagine that there are two Limpets on opposite sides of the same bowl,
with enough little mirrors to reflect the laser beams from one to the other.

Although I'm not sure I can think of a real-world application for this.

How it might work
-----------------

1. The KBUS kernel module has an option to make it issue replier bind/unbind
   messages, of the form:

       * ``$.KBUS.Announce.Replier.Bound``, <message name>, <ksock id of replier>
       * ``$.KBUS.Announce.Replier.Unbound``, <message name>, <ksock id of replier>

   Does that last need the KSock id of the replier that is unbound? It
   probably doesn't hurt, and might well be useful...

   It is important that the "unbound" message also happens when a replier
   "dies" (but this will probably be simpler than otherwise, as it should be
   triggered in the ``release`` function in the kernel module, anyway).

   .. note:: In fact, there already *is* a ``$.KBUS.Replier.Unbound`` message,
      which is issued by the kernel module when a request is not going to be
      answered because the replier (unexpectedly) went away. That's not a
      "broadcast" message, though - it is syntesised internally as a
      substitute for the reply message that the sender was expecting.

2. The Limpet needs to be told which messages to proxy.

   It *may* be enough just to tell it the message names (in some manner) and
   allow it to "look for":

   * Any messages with that name (in which case, proxy them for listening)
   * Any "Announce" messages for Repliers to that name (in which case, proxy
     the binding, and any request messages with said name).

3. The Limpets probably also need to agree on (or be told) netword ids, to
   put into outgoing messages (remember, a message id has a local component
   and a network id).

   It's not yet entirely clear to me whether this last *is* needed - but it
   may well be a useful convenience.

Things to worry about
---------------------

* Whether anything can get "lost" because of things dying unexpectedly on one
  system or the other.

* Whether use of "reply_to" works (i.e., to make sure one is sending a request
  to the replier one sent one to earlier, rather than a new replier). This
  last may take a little thought, or may "just work".

Also, Richard has raised issue 17, which requires some means of synchronising
messages between KBUS devices (this is not an inter-platform problem, though,
as it can also happen on the same machine).

Requirements
------------

If we send a message through kbus, and the message has an id with the network
id set, then kbus must preserve the entire message id. Does it already do
this?

2010-01-22
==========
Friday 22 January

Reporting "replier unbind" events when kbus_release is called
-------------------------------------------------------------

Here's the thing.

First, try to send our message as normal. If that works, all and
good.

If it doesn't work, add all the messages (which we would have sent)
to a set-aside list. Also set a flag on each recipient ksock to say
there may be messages for it on the set-aside list.

When a ksock want to know if it has a next message, if the "maybe
something on the set-aside list" flag is set, first look through
that list to see if there is a message there (before doing the normal
"have I got a next message" check). If there isn't, unset the flag.

When a ksock goes to read the next message, if the flag is set,
it first looks for a message from the set-aside list, and if it
finds one, it returns that instead. It does *not* unset the flag,
because it doesn't know if there is another message waiting for
it on the list. If it doesn't find a message on the set-aside
list, then it clears the flag, and returns the "normal" next
message.

When a ksock releases, if the flag is set, it looks through the
set-aside list and removes any messages for it.

When a ksock unbinds from $.KBUS.ReplierBindEvent, I suspect that
it should check the flag, and if it is set, remove any messages
for it from the set-aside list, and then clear the flag. This last
needs rethinking, because (a) it makes the replier bind event
message even more special, and (b) I'm not 100% sure yet that this
is the expected/correct behaviour from the user-space perspective.

    NB: That message is getting very special. Put a prohibition
    in the "bind" code to forbid anyone from binding to it as a
    Replier.

The set-aside list has a limit on how long it can get. When it
reaches that limit, instead of putting a copy of the UNBIND messages
on the list, a "tragic world" flag is set, and each recipient gets a
"the world has gone tragically wrong" message instead. This does
not attempt to have any data associated with it - the intent is that
the user space programm closes the relevant KSock and restarts.

When the "tragic world" flag is set, an attempt to add a new unbind
message to the list will add a "world gone tragic" message instead,
if the recipient didn't already have one in the list. Note that
searching for these need not be too bad, as they are guaranteed to
be at the end of the list, and all together.

Once the list is empty again (because people have read the messages
off it), the "tragic world" flag gets unset.

NB: Richard reckons that a network up/down event could cause lots
of these events, temporarily, in a Limpet situation, so we really
want to allow quite a few messages in our set-aside list.

---------------------------------------------------

From notes in the code::

	 * Here's the thing.
	 *
	 * First, try to send our message as normal. If that works, all and
	 * good.
	 *
	 * If it doesn't work, add all the messages (which we would have sent)
	 * to a set-aside list. Also set a flag on each recipient ksock to say
	 * there may be messages for it on the set-aside list.
	 *
	 * When a ksock want to know if it has a next message, if the "maybe
	 * something on the set-aside list" flag is set, first look through
	 * that list to see if there is a message there (before doing the normal
	 * "have I got a next message" check). If there isn't, unset the flag.
	 *
	 * When a ksock goes to read the next message, if the flag is set,
	 * it first looks for a message from the set-aside list, and if it
	 * finds one, it returns that instead. It does *not* unset the flag,
	 * because it doesn't know if there is another message waiting for
	 * it on the list. If it doesn't find a message on the set-aside
	 * list, then it clears the flag, and returns the "normal" next
	 * message.
	 *
	 * When a ksock releases, if the flag is set, it looks through the
	 * set-aside list and removes any messages for it.
	 *
	 * When a ksock unbinds from $.KBUS.ReplierBindEvent, I suspect that
	 * it should check the flag, and if it is set, remove any messages
	 * for it from the set-aside list, and then clear the flag. This last
	 * needs rethinking, because (a) it makes the replier bind event
	 * message even more special, and (b) I'm not 100% sure yet that this
	 * is the expected/correct behaviour from the user-space perspective.
	 *
	 *     NB: That message is getting very special. Put a prohibition
	 *     in the "bind" code to forbid anyone from binding to it as a
	 *     Replier.
	 *
	 * The set-aside list has a limit on how long it can get. When it
	 * reaches that limit, instead of putting a copy of the UNBIND messages
	 * on the list, a "tragic world" flag is set, and each recipient gets a
	 * "the world has gone tragically wrong" message instead. This does
	 * not attempt to have any data associated with it - the intent is that
	 * the user space programm closes the relevant KSock and restarts.
	 *
	 * When the "tragic world" flag is set, an attempt to add a new unbind
	 * message to the list will add a "world gone tragic" message instead,
	 * if the recipient didn't already have one in the list. Note that
	 * searching for these need not be too bad, as they are guaranteed to
	 * be at the end of the list, and all together.
	 *
	 * Once the list is empty again (because people have read the messages
	 * off it), the "tragic world" flag gets unset.
	 *
	 * NB: Richard reckons that a network up/down event could cause lots
	 * of these events, temporarily, in a Limpet situation, so we really
	 * want to allow quite a few messages in our set-aside list.


---------------------------------------------------

Hmm. Rather than move a message from the unsent list to the normal message
list at the start of a "next message", it probably makes more sense to do
it just *after* a read. This means the message count (of normal unread
messages) will still be set -- see the test_unsent_bind_event_1 test.

So, reworking the above:

  First, try to send our message as normal. If that works, all and good.

  If it doesn't work, add all the messages (which we would have sent) to a
  set-aside list. Also set a flag on each recipient ksock to say there may be
  messages for it on the set-aside list.

        DONE

  When a ksock asks for the next message (with the NEXTMSG ioctl), then
  retrieve the next message (from the message queue into the "current message
  being read" slot) as usual. After that, however, if the "maybe something in
  the set-aside list" flag is set, look through that list to see if there *is*
  a message there for this ksock. If there is not, clear the flag. If there
  is, move it across into the normal message queue (which now has room for
  it), but don't unset the "maybe something in the set-aside list" flag,
  because it doesn't know if there is another message waiting for it on the
  list.
        
        DONE.

  when kbus_unbind() is called, we may remove some messages from the message
  queue. If we do, then we have room to copy over (at least one) message from
  the set-aside list (if necessary). This will stop the message count
  accidentally dropping to 0 when it shouldn't.

        DONE.

  When a ksock releases, if the flag is set, it looks through the set-aside
  list and removes any messages for it.

        DONE

  When a ksock unbinds from $.KBUS.ReplierBindEvent, it should check the flag,
  and if it is set, remove any messages for it from the set-aside list, and
  then clear the flag. Yes, that makes $.KBUS.ReplierBindEvent even more
  special. So it goes.

        DONE, sort of

  Polling should work as normal, because we're pulling across the set-aside
  messages into the normal queue each time a read is done.

  Asking how many messages are outstanding will give a wrong result, but at
  least it will be the maximum number of messages that may be queued that is
  being returned (and it can only be wrong in this manner if the ksock has
  bound to receive Replier Bind Events).

  The set-aside list has a limit on how long it can get. When it reaches that
  limit, a "gone tragic" flag is set on the list.

  When the "tragic world" flag is set, any attempt to add a new unbind message
  to the list (including the attempt that caused the flag to be set) will add
  a "world gone tragic" message instead, but only if the recipient didn't
  already have one in the list. Note that searching for these need not be too
  bad, as they are guaranteed to be at the end of the list, and all together.

  Note that the "gone tragically wrong" message does not have any data
  associated with it - the intent is that the user space program closes the
  relevant KSock and restarts.

  Once the list is empty again (because people have read the messages off it),
  the "tragic world" flag gets unset.

        DONE

.. note:: Look at error returns and usage of
   kbus_maybe_move_unsent_unbind_msg() and its friends.

.. note:: When issue 23 gets fixed, we will also have to remember the "bound by"
   information for each message in the set-aside list, as (a) the user might
   have bound to $.KBUS.ReplierBindEvent more than once, (b) we'll need to
   set the "bound_by" field in the main message queue anyway, and (c) if
   they have bound to it more than once, when they unbind from one of those
   bindings, we only want to remove the appropriate messages from the
   set-aside list (ick).

   (We can't just forbid people from binding more than once to
   $.KBUS.ReplierBindEvent, because they *are* allowed to bind to (for
   instance) $.KBUS.*, and we don't want to stop that...)

.. note:: In kbus_push_message(), we note that (a) this is the only way to add
   a messsage to the ksock's message queue, (b) the message should already
   have been checked for "sanity", but (c) within the function we check the
   message structure and name again. There's already a comment to remind me to
   check if these checks are redundant (since if they can be removed, the
   function gets to have much simpler error returns). So actually check if
   the extra checks *are* redundant...

.. vim: set filetype=rst tabstop=8 shiftwidth=2 expandtab:
