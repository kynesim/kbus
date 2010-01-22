#! /usr/bin/env python
"""Limpet - a mechanism that proxies KBUS messages to/from another Limpet.

Usage:

    limpet.py  <things>

This runs a client or server limpet, talking to a server or client limpet
(respectively).

The <things> specify what the Limpet is to do. The order of <things> on the
command line is not significant, but if a later <thing> contradicts an earlier
<thing>, the later <thing> wins.

<thing> may be:

    <host>:<port>   Communicate via the specified host and port.
    <path>          Communicate via the named Unix domain socket.

        One or the other communication mechanism must be specified.

    -c, -client     This is a client Limpet.
    -s, -server     This is a server Limpet.

        Either client or server must be specified.

    -id <number>    Messages sent by this Limpet (to the other Limpet) will
                    have network ID <number>. This defaults to 1 for a client
                    and 2 for a server.

    -k <number>, -kbus <number>
                    Connect to the given KBUS device. The default is to connect
                    to KBUS 0.

    -m <name>, -message <name>
                    Proxy any messages with this name to the other Limpet.
                    Using "-m '$.*'" will proxy all messages, and this is
                    the default.
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
#   Tibs <tony.ibbs@gmail.com>
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
       to be sent only once to the Limpet's KSock - which is really what
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
       * the "from" id (to indicate the KSock this message was sent from)
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
    2. ignore the "from" field, and reset it to the KSock id for Limpet:y

Thus Limpet:y needs to remember the correspondence between the Request message
it received, and the message that actually got sent to B. It can, of course,
determine the new message id, and it knows its own KSock id.

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
particular KSock) - the story for this needs writing.

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

import ctypes
import os
import select
import socket
import struct
import sys

from kbus import KSock, Message, Reply, MessageId
from kbus.messages import _MessageHeaderStruct, _struct_from_string
from kbus.messages import split_replier_bind_event_data

MSG_HEADER_LEN = ctypes.sizeof(_MessageHeaderStruct)

class GiveUp(Exception):
    pass

class OtherLimpetGoneAway(Exception):
    """The other end has closed its end of the socket.
    """
    pass

class NoMessage(Exception):
    """There was no message.
    """
    pass

class BadMessage(Exception):
    """We have read a badly formatted KBUS message.
    """
    pass

def msgstr(msg):
    """Return a short string for what we want to know of a message.
    """
    if msg.name == '$.KBUS.ReplierBindEvent':
        is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
        return '<%s %s from %s: %s for %d>'%('Bind' if is_bind else 'Unbind',
                                     str(msg.id) if msg.id else '[0:0]',
                                     msg.from_,
                                     name, binder_id)

    if msg.flags & Message.WANT_A_REPLY:
        if msg.wants_us_to_reply():
            what = 'Request (to us)'
        else:
            what = 'Request'
    elif msg.in_reply_to:
        what = 'Reply (to request %s)'%str(msg.in_reply_to)
    else:
        what = 'Message'

    if msg.is_synthetic():
        flag = ' <synthetic> '
    elif msg.is_urgent():
        flag = ' <urgent> '
    else:
        flag = ''

    return '%s %s %s: %s%s from %s, to %s'%(what, msg.name,
            str(msg.id) if msg.id else '[0:0]', flag, repr(msg.data), msg.from_, msg.to)

def padded_name_len(name_len):
    """Calculate the length of a message name, in bytes, after padding.

    Matches the definition in the kernel module's header file
    """
    return 4 * ((name_len + 1 + 3) // 4)

def padded_data_len(data_len):
    """Calculate the length of message data, in bytes, after padding.

    Matches the definition in the kernel module's header file
    """
    return 4 * ((data_len + 3) // 4)

def entire_message_len(name_len, data_len):
    """Calculate the "entire" message length, from the name and data lengths.

    All lengths are in bytes.

    Matches the definition in the kernel module's header file
    """
    return MSG_HEADER_LEN + padded_name_len(name_len) + \
                            padded_data_len(data_len) + 4

class Limpet(object):
    """A Limpet proxies KBUS messages to/from another Limpet.

    So:

    1. This Limpet communicates with one other Limpet via a (specified) socket.
    2. This Limpet communicates with a specified KBUS device, via a KSock.
    3. This Limpet has a set of messages that it will Listen to, and forward
       to the other Limpet.
    4. This Limpet has a set of messages that it will proxy as a Replier for,
       actually forwarding them to the other Limpet for passing on to another
       KBUS.
    5. This Limpet has a set of messages that it asks its paired Limpet to act
       as such a proxy Replier for.

    And probably some other things I've not yet thought of.
    """

    def __init__(self, kbus_device, network_id, socket_addresss,
            is_server, socket_family, message_name='$.*', verbosity=1):
        """A Limpet has two "ends":

        1. 'kbus_device' specifies which KBUS device it should communicate
           with, via ``ksock = KSock(kbus_device, 'rw')``.

        2. 'socket_addresss' is the address for the socket used to
           communicate with its paired Limpet. This should generally be a
           path name (if communication is with another Limpet on the same
           machine, via Unix domain sockets), or a ``(host, port)`` tuple (for
           communication with a Limpet on another machine, via the internet).

        Messages received from KBUS get sent to the other Limpet.

        Messages sent from the other Limpet get sent to KBUS.

        - kbus_device is which KBUS to open
        - network_id is the network id to set in message ids when we are
          forwarding a message to the other Limpet
        - socket_addresss is the socket address we use to talk to the
          other Limpet
        - is_server is true if we are the "server" of the Limpet pair, false
          if we are the "client"
        - socket_family is AF_UNIX or AF_INET, determining what sort of
          address we want -- a pathname for the former, a <host>:<port>
          string for the latter
        - message_name is the name of the message (presumably a wildcard)
          we are forwarding
        - if verbosity is 0, we don't output any "useful" messages, if it is
          1 we just announce ourselves, if it is 2 (or higher) we output
          information about each message as it is processed.
        """
        self.kbus_device = kbus_device
        self.sock_address = socket_addresss
        self.sock_family = socket_family
        self.is_server = is_server
        self.network_id = network_id
        self.message_name = message_name
        self.verbosity = verbosity

        # We don't know the network id of our Limpet pair yet
        self.other_network_id = None

        if socket_family not in (socket.AF_UNIX, socket.AF_INET):
            raise GiveUp('Socket family is %d, must be AF_UNIX (%s) or AF_INET (%d)'%(socket_family,
                socket.AF_UNIX, socket.AF_INET))

        self.sock = None
        self.listener = None
        self.ksock = KSock(self.kbus_device, 'rw')
        self.ksock_id = self.ksock.ksock_id()

        # A dictionary of { <message_name> : <binder_id> } of the messages
        # we are bound as a "Replier in proxy" for.
        self.replier_for = {}

        # A dictionary of information about each Request that we have proxied
        # as a Replier for that Request. We remember the Request message id as
        # the key, and the from/to information as the data
        self.our_requests = {}

        if is_server:
            self.sock = self.setup_as_server(self.sock_address, self.sock_family)
        else:
            self.sock = self.setup_as_client(self.sock_address, self.sock_family)

        # So we're set up to talk at both ends - now sort out what we're
        # talking about

        try:
            # Note that we only want one copy of a message, even if we were
            # registered as (for instance) both Replier and Listener
            self.ksock.want_messages_once(True)

            # Bind to proxy the requested message name
            self.ksock.bind(self.message_name)

            # We always want to bind for Replier Bind Event messages, as well
            # - since we're only going to get one copy of each message, it is
            # safe to bind to this again, even if the ``message_name`` has
            # implicitly already done that
            self.ksock.bind('$.KBUS.ReplierBindEvent')

            # And ask KBUS to *send* such messages
            self.ksock.report_replier_binds(True)
        except:
            self.close()
            raise

    def _send_network_id(self, sock):
        """Send our pair Limpet our network id.
        """
        sock.sendall('HELO')
        value = socket.ntohl(self.network_id)
        data = struct.pack('!L', value)   # unsigned long, network order
        sock.sendall(data)

    def _read_network_id(self):
        """Read our pair Limpet's network id.
        """
        data = self.sock.recv(4, socket.MSG_WAITALL)
        value = struct.unpack('!L', data)   # unsigned long, network order
        return socket.ntohl(value[0])

    def setup_as_server(self, address, family):
        """Set ourselves up as a server Limpet.

        We start listening, until we get someone connecting to us.
        """
        if self.verbosity > 1:
            print 'Listening on', address

        listener = self.listener = socket.socket(family, socket.SOCK_STREAM)
        # Try to allow address reuse as soon as possible after we've finished
        # with it
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(address)
        try:
            listener.listen(1)
            connection, address = self.listener.accept()

            if self.verbosity:
                print 'Connection accepted from (%s, %s)'%(connection, address)

            # Tell the other end what our network id is
            self._send_network_id(connection)

            return connection
        except:
            self.close()
            raise

    def setup_as_client(self, address, family):
        """Set ourselves up as a client Limpet.
        """
        if family == socket.AF_INET:
            sockname = '%s:%s'%address
        else:
            sockname = address

        try:
            sock = socket.socket(family, socket.SOCK_STREAM)
            sock.connect(address)

            if self.verbosity:
                print 'Connected to "%s" as client'%sockname

            # Tell the other end what our network id is
            self._send_network_id(sock)

            return sock
        except Exception as exc:
            raise GiveUp('Unable to connect to "%s" as client: %s'%(sockname, exc))


    def close(self):
        """Tidy up when we're finished.
        """
        if self.ksock:
            self.ksock.close()
            self.ksock = None

        if self.sock:
            if self.verbosity > 1:
                print 'Closing socket'
            if self.is_server:
                self.sock.close()
            else:
                self.sock.shutdown(socket.SHUT_RDWR)
                self.sock.close()
            self.sock = None

        if self.listener:
            if self.verbosity > 1:
                print 'Closing listener socket'
            self.listener.close()
            self.listener = None
            if self.sock_family == socket.AF_UNIX:
                self.remove_socket_file(self.sock_address)

        if self.verbosity:
            print 'Limpet closed'

    def remove_socket_file(self, name):
        # Assuming this is an address representing a file in the filesystem,
        # delete it so we can use it again...
        try:
            os.remove(name)
        except Exception as err:
            raise GiveUp('Unable to delete socket file "%s": %s'%(name, err))

    def __repr__(self):
        sf = {socket.AF_INET:'socket.AF_INET',
              socket.AF_UNIX:'socket.AF_UNIX'}
        parts = []
        parts.append('kbus_device=%s'%self.kbus_device)
        parts.append('network_id=%d'%self.network_id)
        parts.append('socket_address=%s'%self.sock_address)
        parts.append('is_server=%s'%('True' if self.is_server else 'False'))
        parts.append('socket_family=%s'%sf.get(self.sock_family, self.sock_family))
        if self.message_name != '$.*':
            parts.append('message_name=%s'%repr(self.message_name))

        return 'Limpet(%s)'%(', '.join(parts))

    def __str__(self):
        return 'Limpet from KBUS %d KSock %u via %s'%(self.kbus_device,
                self.ksock_id, self.sock_address)

    def read_message_from_socket(self):
        """Read a message from the other Limpet.

        Returns the corresponding Message instance.
        """

        # All KBUS messages start with the start guard:
        start_guard = self.sock.recv(4, socket.MSG_WAITALL)
        if start_guard == '':
          raise OtherLimpetGoneAway()
        elif start_guard == 'HELO':
            # It's the other Limpet telling us its network id
            self.other_network_id = self._read_network_id()
            if self.other_network_id == self.network_id:
                # We rather rely on unique network ids, and *in particular*
                # that this pair have different ids
                raise GiveUp('This Limpet and its pair both have'
                             ' network id %d'%self.network_id)
            if self.verbosity > 1:
                print 'Other Limpet has network id',self.other_network_id
            raise NoMessage
        elif start_guard != 'kbus': # This is perhaps a bit naughty, relying on byte order
          raise BadMessage('Data read starts with "%s", not "kbus"'%start_guard)

        # So we start with the message header - this is always the same length
        rest_of_header_data = self.sock.recv(MSG_HEADER_LEN-4, socket.MSG_WAITALL)

        header_data = start_guard + rest_of_header_data
        header = _struct_from_string(_MessageHeaderStruct, header_data)
        overall_length = entire_message_len(header.name_len, header.data_len)
        rest_of_message = self.sock.recv(overall_length - MSG_HEADER_LEN, socket.MSG_WAITALL)

        return Message(header_data + rest_of_message)

    def write_message_to_socket(self, msg):
        """Write a Message to the other Limpet.

        Adjust it to reflect our network id.

        Caveats:

        1. What do we *do* about the network id?
        
           If KBUS gave us a message with an unset network id, then it is
           a local message, and we set its network id to our own before we
           pass it on. This means that its serial number is unique within
           our own locality.

           If KBUS gave us a message with a set network id, then we must
           preserve it, as the message serial number is unique within that
           network id, but might be the same as a local message.

           That is, we might well have a local message "called" [0:27], and
           also receive a remote message called [130:27] -- we need to retain
           these as distinct, so we can change the first (to have our own
           "local" network id), but may not change the second.

               (This does, of course, assume that network ids are unique - we
               are going to have to trust that has been set up correctly).

        2. When we change the network id, we actually change it in the message
           given to us, which is a non-obvious SIDE EFFECT - we could instead
           copy the message and change the copy (or amend the "string"
           representation of the message before sending it!)
        """

        # By design (honest, I designed it), if you ask for ``msg.id`` and
        # the id is [0:0], then you get None back.
        # Seriously, that's normally a Good Thing.
        # However, in this instance we want to be able to cope with messages
        # from KBUS itself, which *will* have message id [0:0]. So we need
        # to go "under the hood" a bit, and the following is the current
        # correct way to do so.
        if msg.msg.id.network_id == 0:
            msg.msg.id.network_id = self.network_id

        data = msg.to_string()
        self.sock.sendall(data)

    def handle_message_from_kbus(self, msg):
        """Do the appropriate thing with a message from KBUS.

        We take a message read from KBUS, and write it to the other Limpet.

        * ReplierBindEvent - if this is a "reflection" of us binding as a
          (proxy) replier, then ignore it, otherwise forward it to the other
          Limpet so it can bind as a proxy.

        * Request marked WANT_YOU_TO_REPLY - KBUS thinks we're the Replier for
          this message, but it is actually someone beyond the other Limpet, who
          we are proxying for. Send the message through.

        * Request not marked WANT_YOU_TO_REPLY - this is just a normal "listen"
          message, so we treat it as "anything else" below.

        * Reply - we got this because KBUS thinks we're the original Sender,
          for whom we are actually proxying. Send it on to the other Limpet.

        * Status - synthetic messages from KBUS itself, and these are
          essentially special forms of Reply, but with message id [0:0].
          We still need to send them to the other Limpet because they *are*
          the Reply for an earlier Request.

        * anything else - just send it through, with the appropriate changes to
          the message id's network id.
        """
        kbus_name   = 'KBUS%u'%self.kbus_device
        limpet_name = 'Limpet%d'%self.other_network_id
        kbus_hdr  = '%s->Us%s'%(kbus_name, ' '*(len(limpet_name)-2))
        limpet_hdr = '%s->%s'%(' '*len(kbus_name), limpet_name)

        if self.verbosity > 1:
            print '%s %s'%(kbus_hdr, msgstr(msg)),

        if msg.name == '$.KBUS.ReplierBindEvent':
            # If this is the result of *us* binding as a replier (by proxy),
            # then we do *not* want to send it to the other Limpet!
            is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
            if binder_id == self.ksock_id:
                if self.verbosity > 1:
                    print ' which is us -- ignore'
                return

        elif msg.in_reply_to:                   # a Reply (or Status)
            pass            # send it on

        elif msg.flags & Message.WANT_A_REPLY:  # a Request
            if msg.flags & Message.WANT_YOU_TO_REPLY:
                # Remember the details of this Request for when we get a Reply
                key = (msg.msg.id.network_id, msg.msg.id.serial_num)
                self.our_requests[key] = (msg.from_, msg.to)
                # and send it on
            else:
                pass        # send it on

        else: # It's a "normal" message, and Announcement
            pass

        if msg.msg.id.network_id == self.other_network_id:
            # This is a message that originated with our pair Limpet (so it's
            # been from the other Limpet, to us, to KBUS, and we're now getting
            # it back again). Therefore we want to ignore it. When the original
            # message was sent to the other KBUS (before any Limpet touched
            # it), any listeners on that side would have heard it from that
            # KBUS, so we don't want to send it back to them yet again...
            if self.verbosity > 1:
                print ' from the other Limpet -- ignore'
            return

        if self.verbosity > 1:
            print
        self.write_message_to_socket(msg)
        if self.verbosity > 1:
            print '%s %s'%(limpet_hdr, msgstr(msg))  # i.e., with amended network id

    def handle_message_from_socket(self, msg):
        """Do the appropriate thing with a message from the socket.

        We take a message read from the other Limpet, and write it to KBUS.

        * ReplierBindEvent - we never pass these on (when we bind/unbind to the
          local KBUS, it will generate its own if necessary). This acts as a
          signal to us to bind/unbind (as a proxy).

        * Request marked WANT_YOU_TO_REPLY - it's not us that should reply,
          but the original sender. Remove the flag, adjust ids as normal, and
          throw it at our local KBUS, who should know who is bound as a Replier
          on this side.

        * Request not marked WANT_YOU_TO_REPLY - this is just a normal "listen"
          message, so we treat it as "anything else" below.

        * Reply - we got this because someone replied to the other Limpet. They
          only do that if we're proxying for the actual Sender, who is beyond
          the other Limpet. So we need to change its in_reply_to to match local
          conditions (we should have remembered the message id from the
          original Request when it came past us earlier on -- see
          handle_message_from_kbus()), and then send it on.

        * Status - synthetic messages from KBUS itself, and these are
          essentially special forms of Reply, but with message id [0:0].
          These need to be sent on like any other Reply.

        * anything else - just send it through, with the appropriate changes to
          the message id's network id.
        """
        kbus_name  = 'KBUS%u'%self.kbus_device
        limpet_name = 'Limpet%d'%self.other_network_id
        limpet_hdr  = '%s->Us%s'%(limpet_name, ' '*(len(kbus_name)-2))
        kbus_hdr   = '%s->%s'%(' '*len(limpet_name), kbus_name)

        if self.verbosity > 1:
            print '%s %s'%(limpet_hdr, msgstr(msg))

        if msg.name == '$.KBUS.ReplierBindEvent':
            # We have to bind/unbind as a Replier in proxy
            is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
            if is_bind:
                if self.verbosity > 1:
                    print '%s BIND "%s'%(' '*len(limpet_hdr),name)
                self.ksock.bind(name, True)
                self.replier_for[name] = binder_id
            else:
                if self.verbosity > 1:
                    print '%s UNBIND "%s'%(' '*len(limpet_hdr),name)
                self.ksock.unbind(name, True)
                del self.replier_for[name]
            return

        elif msg.in_reply_to:                   # a Reply (or Status)
            # Only the intended recipient (the Sender) receives a Reply,
            # so we don't have to worry about multiple copies of it

            # If this message is in reply to a message from our network,
            # revert to the original message id
            if msg.msg.in_reply_to.network_id == self.network_id:
                msg.msg.in_reply_to.network_id = 0

            # Look up the original Request and amend appropriately
            key = (msg.msg.in_reply_to.network_id, msg.msg.in_reply_to.serial_num)
            try:
                from_, to = self.our_requests[key]
                del self.our_requests[key]          # we shouldn't see it again

                # What if it's a Status message? A Status message is one with
                # a message id of [0:0] (at least when it is generated), which
                # of course will not be preserved when we *send* it to KBUS
                # ourselves.
                #
                # Status messages are:
                #
                # * $.KBUS.Replier.GoneAway - sent when the "to be read"
                #   message queue is being emptied, because the KSock is being
                #   closed.
                # * $.KBUS.Replier.Ignored - sent when the "unsent" message
                #   queue is being emptied, because the KSock is being closed.
                # * $.KBUS.Replier.Unbound - sent when the KSock unbinds from
                #   the message name, and there is a message of that name in
                #   the "to be read" message queue.
                # * $.KBUS.Replier.Disappeared - sent when polling tries to
                #   send a message again, but the Replier has gone away.
                # * $.KBUS.ErrorSending - sent when something went wrong in
                #   the "send a message" mechanisms.
                #
                # So all of these *do* need sending on to KBUS, and I think
                # we don't really care about the message id, or whatever.
                #
                # Note that $.KBUS.ReplierBindEvent is not a Status message in
                # this sense, since it is not a Reply. So that's good, because
                # it means we can treat a Status Reply just like any other Reply.

                # XXX Caveat: *Should* Status messages have a "proper" message
                # XXX         id? Since they don't, they don't participate in
                # XXX         the message ordering that message ids give us...

                # So, we want to send the Reply on to our KBUS
                # The simplest thing to do really is creating a whole new message
                msg = Reply(msg.name, data=msg.data,
                            in_reply_to=MessageId(key[0],key[1]), to=from_)
            except KeyError:
                # We already dealt with this Reply once, so this is presumably
                # a "listening" copy - ignore it, the KBUS at this end will
                # let anyone who cares (at this end) have their own copies
                if self.verbosity > 1:
                    print '%s ignored as a "listen" copy'%(' '*len(limpet_hdr))
                return

            if self.verbosity > 1:
                print '%s as %s'%(' '*(len(limpet_hdr)-3), msgstr(msg))

        elif msg.flags & Message.WANT_A_REPLY:  # a Request
            if msg.flags & Message.WANT_YOU_TO_REPLY:
                # KBUS will set/unset the WANT_YOU_TO_REPLY flag for us,
                # so we don't need to.
                pass
            else:
                pass

        else:
            pass

        try:
            self.ksock.send_msg(msg)
            if self.verbosity > 1:
                print '%s %s'%(kbus_hdr, msgstr(msg))
        except IOError as exc:
            print '%s send_msg: %s -- continuing'%(kbus_hdr, exc)


    def run_forever(self, termination_message):
        """Or until we're interrupted, or receive the termination message.

        If an exception is raised, then the Limpet is closed as the method
        is exited.
        """
        try:
            while 1:
                # Wait for a message written to us, with no timeout
                # (at least for the moment)
                (r, w, x) = select.select( [self.ksock, self.sock], [], [])

                if self.verbosity > 1:
                    print

                if self.ksock in r:
                    msg = self.ksock.read_next_msg()
                    if msg.name == termination_message:
                        raise GiveUp('Termination requested via %s message'%termination_message)
                    self.handle_message_from_kbus(msg)

                if self.sock in r:
                    try:
                        msg = self.read_message_from_socket()
                        self.handle_message_from_socket(msg)
                    except NoMessage:       # Presumably, a HELO <network_id>
                        pass
        finally:
            self.close()

    # Implement 'with'
    def __enter__(self):
        return self

    def __exit__(self, etype, value, tb):
        if tb is None:
            # No exception, so just finish normally
            self.close()
        else:
            # An exception occurred, so do any tidying up necessary
            # - well, there isn't anything special to do, really
            self.close()
            # And allow the exception to be re-raised
            return False


def run_a_limpet(is_server, address, family, kbus_device, network_id,
                 message_name='$.*', termination_message=None, verbosity=1):
    """Run a Limpet.
    """
    print 'Limpet: %s via %s for KBUS %d, using network id %d'%('server' if is_server else 'client',
            address, kbus_device, network_id)

    with Limpet(kbus_device, network_id, address, is_server, family,
                message_name, verbosity) as l:
        if verbosity:
            print l
            if termination_message:
                print "Terminate by sending a message called '%s'"%termination_message
        l.run_forever(termination_message)

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
    run_a_limpet(is_server, address, family, kbus_device, network_id, message_name)

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
