#! /usr/bin/env python
"""runlimpet.py - a simple command line interface to run a KBUS Limpet

Usage:

    runlimpet.py  <things>

This runs a client or server limpet, talking to a server or client limpet
(respectively).

The <things> specify what the Limpet is to do. The order of <things> on the
command line is not significant, but if a later <thing> contradicts an earlier
<thing>, the later <thing> wins.

<thing> may be:

    <host>:<port>   Communicate via the specified host and port
                    (the <host> is ignored on the 'server').
    <path>          Communicate via the named Unix domain socket.

        One or the other communication mechanism must be specified.

    -s, -server     This is a server Limpet.
    -c, -client     This is a client Limpet.

        Either client or server must be specified.

    -id <number>    Messages sent by this Limpet (to the other Limpet) will
                    have network ID <number>. This defaults to 1 for a client
                    and 2 for a server. Regardless, it must be greater than
                    zero.

    -k <number>, -kbus <number>
                    Connect to the given KBUS device. The default is to connect
                    to KBUS 0.

    -m <name>, -message <name>
                    Proxy any messages with this name to the other Limpet.
                    Using "-m '$.*'" will proxy all messages, and this is
                    the default.

    -v <n>          Verbosity level. Default is 1. 0 means less, 2 means more.
"""

# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is the KBUS Lightweight Linux-kernel mediated
# message system
#
# The Initial Developer of the Original Code is Kynesim, Cambridge UK.
# Portions created by the Initial Developer are Copyright (C) 2009
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Kynesim, Cambridge UK
#   Tibs <tibs@tonyibbs.co.uk>
#
# ***** END LICENSE BLOCK *****

documentation = """\
===============
How things work
===============

Consider a single KBUS as a very simple "black box", and a pair of KBUS's
linked by Limpets as a rather fatter "black box". Ideally, the processes
talking to either "side" of a black box should not need to care about which
it is.

So we have::

        A --------> Just-KBUS -------> B

and we also have::

        A --------> KBUS/Limpet:x/~~>/Limpet:y/KBUS --------> B

The only difference we *want* to see is that a message from A to B on the same
KBUS should have an id of the form [0:n] (i.e., no network id), whereas a
message from A to V over a Limpet proxy should have an id of the form [x:n]
(i.e., the network id shows it came via a Limpet with network id 'x').

Complexities may arise for three reasons:

    1. A Limpet does not necessarily have to forward all messages (the default
       is presumably to forward '$.*', subject to solving (1) and (2) above).
    2. A is also listening to messages - we don't want it to hear too many
    3. B binds as a Replier - we want it to be able to reply to messages from A

.. note:: There's a question in that last which will also need answering,
   namely, do Limpets always listen to '$.*', or is it reasonable to be able
   to limit them? If we can limit them, what happens if we accidentally end
   up asking them to listen to the same message more than once?

The Golden Rule of Limpet communication
---------------------------------------
If your Ksocks are communicating via Limpets, then you must remember to use
``ksock.wait_for_msg()``, instead of ``ksock.read_next_msg()``, because you
don't know how long it will take for a message to work its way through the
"network".

Looking at 1: A Limpet does not have to forward all messages
------------------------------------------------------------
The possibilities are broadly:

    1. Limpets always listen for '$.*'

       This is the simplest option, but wastes the maximum bandwith (since it
       seems likely that we might know that only a subset of messages are of
       interest to the other Limpet's system).

    2. Limpets default to listening for '$.*', but the user may replace
       that with a single (possibly wildcarded) name, e.g., '$.Fred.%'

       This is the next simplest option. It retains much of the simplicity of
       the first option, but allows us to not send some (many) messages. It
       does, however, assume that a single wildcarding is enough to do the
       restriction we wish.

       For the moment, I think this is my favoured option, as it seems to be a
       good middle ground.

    3. Limpets default to listening for '$.*', but the user may replace
       that with more than one (possibly wildcarded) name. That may
       cause philosophical problems if they choose (for instance) '$.Fred.*'
       and '$.Fred.Jim', since the Limpet will "hear" some messages twice.

       This is the most flexible option, but is most likely to cause problems
       when the user specifies overlapping wildcards (if it's possible, we have
       to support it). I'd like to avoid it unless it is needed.

There's also a variant of each of the second and third where the Limpet doesn't
have a default at all, and thus won't transmit anything until told what to do.
But I think that is sufficiently similar not to worry about separately.

Remember that Limpets will always transmit Replies (things that they are
proxying as Sender for).

    .. note:: Limpets must always listen to $.KBUS.ReplierBindEvent. If we
       allow them to choose to listen to something other then '$.*', then
       we also need to CHECK that the thing-listened-to includes the replier
       bind events, and if not, add them in as well.
       
       Which is luckily solved if I use MSGONLYONCE to ask for messages
       to be sent only once to the Limpet's Ksock - which is really what
       I want anyway, I think...

Looking at 2: A is also listening to messages
---------------------------------------------

Let us assume A is listening to '$.*' - that is, all messages.

In the "normal" case, when A sends a message (any message) it will also get to
read that message back, precisely once for each time it is bound to '$.*' as a
Listener. It may also get it back for other reasons (because it was sending a
Request and is also a Replier, or because it is listening to the message with a
more specific binding name), but if solve the one then the others come for free.

So we want the same behaviour for the case with Limpets. And lo, when A sends
the message to its own KBUS, that same KBUS will do all the mirroring (to A)
that is necessary.

Limpet:x still needs to forward it (in case B wants to hear it - and unless
Limpet:x has been told not to). If we assume Limpet:y passes it on, and is
itself listening for '$.*' (a sensible assumption), then we also know that
Limpet:y will receive it back again.

We also know that the message received back by Limpet:y will have its network
id set to x (since KBUS won't have changed it), so (if we knew x) we could just
make a rule that messages with their network id set to the other Limpet's
network id shouldn't be sent back.

That assumes we have a simple way of knowing that network id. We could hack
that by remembering it off the first message we received from Limpet:x, given
we only talk to one other Limpet, if, and only if, all messages from Limpet:x
actually have that network id. Which I'm not sure about, since I don't yet know
what we're going to do about situations like::

        A --------> K/L:x/~~>/L:y/K/L:u/~~>/L:v/K --------> B

which we can't exactly forbid, and so will have to deal with.

So rather than that, the solution may just be to make Limpet:y remember the
message ids of any messages it receives from Limpet:x *that also match those it
is Listening for*, and "cross them off" when it gets them back from KBUS.

That is:

    * receive a message from the other Limpet
    * if its name matches those we're bound to Listen for, remember its id
    * when we read a message from KBUS, if it's not a message we're the
      replier for (i.e., if we got it as a Listener), and its id is one we've
      remembered, then:
      
      1. don't send it to the other Limpet
      2. forget its id -- this assumes that we're only listening to this
         message name once, if we allow otherwise, then we need to be a bit
         more careful

Looking at 3: B binds as a Replier
----------------------------------
B binds as a replier, and thus Limpet:x has to bind as a Replier for the same
message name.

    .. note:: From the KBUS documentation

       **Things KBUS changes in a message**

       In general, KBUS leaves the content of a message alone - mostly so that an
       individual KBUS module can *pass through* messages from another domain.
       However, it does change:

       * the message id's serial number (but only if its network id is unset)
       * the "from" id (to indicate the Ksock this message was sent from)
       * the WANT_YOU_TO_REPLY bit in the flags (set or cleared as appropriate)
       * the SYNTHETIC bit, which will always be unset in a message sent by a Sender

A sends a Request with that name, which gets sent to Limpet:x marked "you
should reply", and with the "from" field set to A.

Limpet:x passes it on to Limpet:y (amending the message ids network id to x,
and amending the "from" fields network id to x as well). Limpet:x remembers
that it has done this (by remembering the message id).

Limpet:y then sends the message to its KBUS, which will:

    1. ignore the "you should reply" flag, but reset it for the message
       that gets sent to B
    2. ignore the "from" field, and reset it to the Ksock id for Limpet:y

Thus Limpet:y needs to remember the correspondence between the Request message
it received, and the message that actually got sent to B. It can, of course,
determine the new message id, and it knows its own Ksock id.

B then receives the message, marked that *it* should reply. Presumably it does so.

In that case, Limpet:y receives the message (as Sender). It can recognise what
it is a reply to, because of the "in_reply_to" field. It sets the network id
for the message id, and for the "from" field. It needs to replace that
"in_reply_to" field with the saved (original) message id, and send the whole
thing back to Limpet:x.

Limpet:x is acting as Replier for this message on its side. It needs to set the
"in reply to" field to the message id of the original Request, i.e., removing
the network id.

NB: I think the above works for "synthetic" messages as well, but of course if
B's KBUS says that B has gone away, then the Limpet needs to do appropriate
things (such as, for instance, forgetting about any outstanding messages - this
may need action by both Limpets). B going away should, of course, generate a
Replier Unbind Event.

Deferred for now: Requests with the "to" field set (i.e., to Request a
particular Ksock) - the story for this needs writing.

Things to worry about
=====================
Status messages (KBUS synthetic messsages) and passing them over multiple
Limpet boundaries...

Remember that synthetic messages (being from KBUS itself) have message id
[0:0] (which shows up as None when we ask for msg.id)

We can't *send* a synthetic message, because we can't send a message with id
[0:0] - once we've passed it on to KBUS, it will get assigned a "proper"
message id.

Moreover, we probably don't want to - what we need to do instead is to provoke
a corresponding event - which normally means "going away" like the Replier
presumably did. On the other hand, if a Replier does "go away", we should get a
Replier Unbind Event, which causes us to unbind, which should have the same
effect...
"""

import os
import socket
import sys

from kbus.limpet import run_a_limpet, GiveUp, OtherLimpetGoneAway

def parse_address(word):
    """Work out what sort of address we have.

    Returns (address, family)
    """
    if ':' in word:
        try:
            host, port = word.split(':')
            port = int(port)
            address = host, port
            family = socket.AF_INET
        except Exception as exc:
            raise GiveUp('Unable to interpret "%s" as <host>:<port>: %s'%(word, exc))
    else:
        # Assume it's a valid pathname (!)
        address = word
        family = socket.AF_UNIX
    return address, family

def main(args):
    """Work out what we've been asked to do and do it.
    """
    is_server = None            # no default
    address = None              # ditto
    kbus_device = 0
    network_id = None
    message_name = '$.*'
    verbosity = 1

    if not args:
        print __doc__
        return

    while args:
        word = args[0]
        args = args[1:]

        if word.startswith('-'):
            if word in ('-c', '-client'):
                is_server = False
            elif word in ('-s', '-server'):
                is_server = True
            elif word in ('-id'):
                try:
                    network_id = int(args[0])
                except:
                    raise GiveUp('-id requires an integer argument (network id)')
                if network_id < 1:
                    raise GiveUp('Illegal network id (-id %d), network id must'
                                 ' be > 0'%network_id)
                args = args[1:]
            elif word in ('-m', '-message'):
                try:
                    message_name = args[0]
                except:
                    raise GiveUp('-message requires an argument (message name)')
                args = args[1:]
            elif word in ('-k', '-kbus'):
                try:
                    kbus_device = int(args[0])
                except:
                    raise GiveUp('-kbus requires an integer argument (KBUS device)')
                args = args[1:]
            elif word == '-v':
                try:
                    verbosity = int(args[0])
                except:
                    raise GiveUp('-v requires an argument (verbosity number)')
                args = args[1:]
            else:
                print __doc__
                return
        else:
            # Deliberately allow multiple "address" values, using the last
            address, family = parse_address(word)

    if is_server is None:
        raise GiveUp('Either -client or -server must be specified')

    if address is None:
        raise GiveUp('An address (either <host>:<port> or < is needed')

    if network_id is None:
        network_id = 2 if is_server else 1

    # And then do whatever we've been asked to do...
    run_a_limpet(is_server, address, family, kbus_device, network_id,
                 message_name, verbosity=verbosity)

if __name__ == "__main__":
    args = sys.argv[1:]
    try:
        main(args)
    except GiveUp as exc:
        print exc
    except OtherLimpetGoneAway as exc:
        print 'The Limpet at the other end of the connection has closed'
    except KeyboardInterrupt:
        pass

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
