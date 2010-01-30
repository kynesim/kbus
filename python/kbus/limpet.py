"""Limpet - a mechanims for proxying KBUS messages to/from another Limpet.

This allows messages to be communicated from one KBUS device to another,
either on the same machine or over a network.
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

import ctypes
import errno
import os
import select
import socket
import struct
import sys

from kbus import Ksock, Message, Reply, MessageId
from kbus.messages import _MessageHeaderStruct, _struct_from_string
from kbus.messages import split_replier_bind_event_data

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

class Limpet(object):
    """A Limpet proxies KBUS messages to/from another Limpet.

    So:

    1. This Limpet communicates with one other Limpet via a (specified) socket.
    2. This Limpet communicates with a specified KBUS device, via a Ksock.
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
           with, via ``ksock = Ksock(kbus_device, 'rw')``.

        2. 'socket_addresss' is the address for the socket used to
           communicate with its paired Limpet. This should generally be a
           path name (if communication is with another Limpet on the same
           machine, via Unix domain sockets), or a ``(host, port)`` tuple (for
           communication with a Limpet on another machine, via the internet).

        Messages received from KBUS get sent to the other Limpet.

        Messages sent from the other Limpet get sent to KBUS.

        - kbus_device is which KBUS to open
        - network_id is the network id to set in message ids when we are
          forwarding a message to the other Limpet. It must be greater
          than zero.
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
        if network_id < 1:
            raise ValueError('Limpet network id must be > 0, not %d'%network_id)

        if socket_family not in (socket.AF_UNIX, socket.AF_INET):
            raise ValueError('Socket family is %d, must be AF_UNIX (%s) or'
                             ' AF_INET (%d)'%(socket_family,
                                              socket.AF_UNIX, socket.AF_INET))

        self.kbus_device = kbus_device
        self.sock_address = socket_addresss
        self.sock_family = socket_family
        self.is_server = is_server
        self.network_id = network_id
        self.message_name = message_name
        self.verbosity = verbosity

        # We don't know the network id of our Limpet pair yet
        self.other_network_id = None

        self.sock = None
        self.listener = None
        self.ksock = Ksock(self.kbus_device, 'rw')
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
        return 'Limpet from KBUS %d Ksock %u via %s'%(self.kbus_device,
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
        elif start_guard != 'Kbus': # This is perhaps a bit naughty, relying on byte order
          raise BadMessage('Data read starts with "%s", not "Kbus"'%start_guard)

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
        if msg._id.network_id == 0:
            msg._id.network_id = self.network_id

        # Limpets are responsible for setting the 'orig_from' field,
        # which indicates:
        #
        # 1. The ksock_id of the original sender of the message
        # 2. The network_id of the first Limpet to pass the message
        #    on to its pair.
        #
        # When the message gets *back* to this Limpet, we will be
        # able to recognise it (its network id will be the same as
        # ours), and thus we will know the ksock_id of its original
        # sender, if we care.
        #
        # Moreover, we can use this information when setting up a
        # stateful request - the orig_from can be copied to the
        # stateful request's final_to field, the network/ksock we
        # want to assert must handle the far end of the dialogue.
        #
        # So, if we are the first Limpet to handle this message from
        # KBUS, then we give it our network id.
        if msg._orig_from.network_id == 0:
            if self.verbosity > 1:
                print '+++++++++ setting orig_from to %u,%u'%(self.network_id,msg.from_)
            msg._orig_from.network_id = self.network_id
            msg._orig_from.local_id = msg.from_

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
            print '%s %s'%(kbus_hdr, str(msg)),

        if msg.name == '$.KBUS.ReplierBindEvent':
            # If this is the result of *us* binding as a replier (by proxy),
            # then we do *not* want to send it to the other Limpet!
            is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
            if binder_id == self.ksock_id:
                if self.verbosity > 1:
                    print ' which is us -- ignore'
                return

        elif msg.is_reply():                   # a Reply (or Status)
            pass

        elif msg.is_request():
            if msg.flags & Message.WANT_YOU_TO_REPLY:
                # Remember the details of this Request for when we get a Reply
                # (Note that the message id itself is not suitable as a key,
                # as it is not immutable, and does not have a __hash__ method)
                key = (msg._id.network_id, msg._id.serial_num)
                self.our_requests[key] = (msg.from_, msg.to)
                # and send it on
            else:
                pass        # send it on

        else: # It's a "normal" message, an Announcement
            pass

        if msg._id.network_id == self.other_network_id:
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
            print '%s %s'%(limpet_hdr, str(msg))  # i.e., with amended network id

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
            print '%s %s'%(limpet_hdr, str(msg))

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

        elif msg.is_reply():                   # a Reply (or Status)
            # Only the intended recipient (the Sender) receives a Reply,
            # so we don't have to worry about multiple copies of it

            # If this message is in reply to a message from our network,
            # revert to the original message id
            if msg._in_reply_to.network_id == self.network_id:
                msg._in_reply_to.network_id = 0

            # Look up the original Request and amend appropriately
            key = (msg._in_reply_to.network_id, msg._in_reply_to.serial_num)
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
                #   message queue is being emptied, because the Ksock is being
                #   closed.
                # * $.KBUS.Replier.Ignored - sent when the "unsent" message
                #   queue is being emptied, because the Ksock is being closed.
                # * $.KBUS.Replier.Unbound - sent when the Ksock unbinds from
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

                # So, we want to send the Reply on to our KBUS
                # The simplest thing to do really is creating a whole new message
                msg = Reply(msg.name, data=msg.data,
                            in_reply_to=MessageId(key[0],key[1]),
                            to=from_, orig_from=msg.orig_from)
            except KeyError:
                # We already dealt with this Reply once, so this is presumably
                # a "listening" copy - ignore it, the KBUS at this end will
                # let anyone who cares (at this end) have their own copies
                if self.verbosity > 1:
                    print '%s ignored as a "listen" copy'%(' '*len(limpet_hdr))
                return

            if self.verbosity > 1:
                print '%s as %s'%(' '*(len(limpet_hdr)-3), str(msg))

        elif msg.is_stateful_request() and msg.wants_us_to_reply():
            # The Request will have been marked "to" our Limpet pair
            # (otherwise we would not have received it).
            #
            # We now need to work out what the 'to' field needs setting
            # to for the next step on the messages route.

            # If the 'final_to' has a network id that matches ours,
            # then we need to unset that, as it has clearly now come
            # into its "local" network.
            if self.verbosity > 1:
                print '%s *** final_to.network_id %u, network_id %u'%(kbus_hdr,
                              msg._final_to.network_id, self.network_id)
            if msg._final_to.network_id == self.network_id:
                msg._final_to.network_id = 0        # XXX Do we need to do this?
                is_local = True
            else:
                is_local = False

            # Find out who KBUS thinks is replying to this message name
            replier_id = self.ksock.find_replier(msg.name)
            if replier_id is None:
                # Oh dear - there is no replier
                if self.verbosity > 1:
                    print '%s *** There is no Replier - Replier gone away'%kbus_hdr
                error = Message('$.KBUS.Replier.GoneAway',
                                to=msg.from_,
                                in_reply_to=msg.id)
                self.write_message_to_socket(error)
                return

            if self.verbosity > 1:
                print '%s *** %s, kbus replier %u'%(kbus_hdr,
                        'Local' if is_local else 'Nonlocal',replier_id)

            if is_local:
                # The KBUS we're going to write the message to is
                # the final KBUS. Thus the replier id must match
                # that of the original Replier
                if replier_id != msg._final_to.local_id:
                    # Oops - wrong replier - someone rebound
                    if self.verbosity > 1:
                        print '%s *** Replier is %u, wanted %u - ' \
                              'Replier gone away'%(kbus_hdr,replier_id,msg._final_to.local_id)
                    error = Message('$.KBUS.Replier.NotSameKsock', # XXX New message name
                                    to=msg.from_,
                                    in_reply_to=msg.id)
                    self.write_message_to_socket(error)
                    return

            # Regardless, we believe the message is OK, so need to
            # adjust who it is meant to go to (locally)
            if is_local:
                # If we're in our final stage, then we insist that the
                # Replier we deliver to be the Replier we expected
                msg.msg.to = msg._final_to.local_id
            else:
                # If we're just passing through, then just deliver
                # it to whoever is listening
                # XXX What happens if they are not a Limpet?
                # XXX That would be bad - but I'm not sure how we could
                # XXX tell (short of allowing Limpets to register with
                # XXX KBUS, so that we can ask - and then a non-Limpet
                # XXX could presumably *pretend* to be a Limpet anyway
                # XXX - a potentially infinite shell game then ensues...
                msg.msg.to = replier_id

            if self.verbosity > 1:
                print '%s Adjusted the msg.to field'%kbus_hdr

        else:
            pass

        try:
            self.ksock.send_msg(msg)
            if self.verbosity > 1:
                print '%s %s'%(kbus_hdr, str(msg))
        except IOError as exc:
            # If we were sending a Request, we need to fake an
            # appropriate Reply.
            if msg.is_request():
                try:
                    errname = '$.KBUS.RemoteError.%s'%errno.errorcode[exc.errno]
                except KeyError:
                    errname = '$.KBUS.RemoteError.%d'%exc.errno
                if self.verbosity > 1:
                    print '%s *** Remote error %s'%(kbus_hdr, errname)
                error = Message(errname,
                                to=msg.from_,
                                in_reply_to=msg.id)
                self.write_message_to_socket(error)
                return
            #
            # If we were sending a Reply, we need to think whether we
            # can do anything useful...
            #
            # XXX TODO
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

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
