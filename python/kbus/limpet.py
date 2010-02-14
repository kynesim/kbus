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

from socket import ntohl, htonl

from kbus import Ksock, Message, Reply, MessageId, OrigFrom
from kbus.messages import _MessageHeaderStruct, _ReplierBindEventHeader, \
        message_from_parts, _struct_from_string, _struct_to_string, \
        split_replier_bind_event_data, \
        calc_padded_name_len, calc_padded_data_len, calc_entire_message_len, \
        MSG_HEADER_LEN

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

_SERIALISED_MESSAGE_HEADER_LEN = 16
_SerialisedMessageHeaderType = ctypes.c_uint32 * _SERIALISED_MESSAGE_HEADER_LEN

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

    Note that the Limpet sets various things (including requesting Replier Bind
    Events from its KBUS) when it starts up, and makes no attempt to restore
    any changes when (if) it closes.
    """

    def __init__(self, ksock, sock, network_id, message_name='$.*', verbosity=1):
        """A Limpet has two "ends":

        1. 'ksock' specifies which KBUS device it should communicate
           with. This should have been opened for read and write.
        2. 'sock' is the socket used to communicate with its paired Limpet.

        Messages received from KBUS get sent to the other Limpet.

        Messages sent from the other Limpet get sent to KBUS.

        - ksock is the KBUS connection
        - sock is the socket to the other Limpet
        - network_id is the network id to set in message ids when we are
          forwarding a message to the other Limpet. It must be greater
          than zero.
        - message_name is the name of the message (presumably a wildcard)
          we are forwarding
        - if verbosity is 0, we don't output any "useful" messages, if it is
          1 we just announce ourselves, if it is 2 (or higher) we output
          information about each message as it is processed.
        """
        if network_id < 1:
            raise ValueError('Limpet network id must be > 0, not %d'%network_id)

        self.ksock = ksock
        self.sock = sock
        self.network_id = network_id
        self.message_name = message_name
        self.verbosity = verbosity

        # We don't know the network id of our Limpet pair yet
        self.other_network_id = None

        self.ksock_id = self.ksock.ksock_id()

        # A dictionary of { <message_name> : <binder_id> } of the messages
        # we are bound as a "Replier in proxy" for.
        self.replier_for = {}

        # A dictionary of information about each Request that we have proxied
        # as a Replier for that Request. We remember the Request message id as
        # the key, and the from/to information as the data
        self.our_requests = {}

        # So we're set up to talk at both ends - now sort out what we're
        # talking about

        try:
            # Swap network ids with our pair
            self._send_network_id()
            self._read_network_id()

            if verbosity > 1:
                self.ksock.kernel_module_verbose(True)

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

    def _send_network_id(self):
        """Send our pair Limpet our network id.
        """
        self.sock.sendall('HELO')
        value = htonl(self.network_id)
        data = struct.pack('!L', value)   # unsigned long, network order
        self.sock.sendall(data)

    def _read_network_id(self):
        """Read our pair Limpet's network id.
        """
        hello = self.sock.recv(4, socket.MSG_WAITALL)
        if hello == '':
          raise OtherLimpetGoneAway()
        elif hello != 'HELO':
            raise BadMessage("Expected 'HELO' to announce other limpet,"
                             " got '%s'"%hello)

        data = self.sock.recv(4, socket.MSG_WAITALL)
        value = struct.unpack('!L', data)   # unsigned long, network order
        self.other_network_id = ntohl(value[0])

        if self.other_network_id == self.network_id:
            # We rather rely on unique network ids, and *in particular*
            # that this pair have different ids
            raise GiveUp('This Limpet and its pair both have'
                         ' network id %d'%self.network_id)
        if self.verbosity > 1:
            print 'Other Limpet has network id',self.other_network_id

    def close(self):
        """Tidy up when we're finished.
        """
        if self.verbosity:
            print 'Limpet closed'

    def __repr__(self):
        sf = {socket.AF_INET:'socket.AF_INET',
              socket.AF_UNIX:'socket.AF_UNIX'}
        parts = []
        parts.append('ksock=%s'%self.ksock)
        parts.append('sock=%s'%self.sock)
        parts.append('network_id=%d'%self.network_id)
        if self.message_name != '$.*':
            parts.append('message_name=%s'%repr(self.message_name))
        if self.verbosity != 1:
            parts.append('verbosity=%s'%self.verbosity)

        return 'Limpet(%s)'%(', '.join(parts))

    def __str__(self):
        return 'Limpet from KBUS Ksock %u via socket %s'%(self.ksock_id,
                                                          self.sock)

    def serialise_message_header(self, msg):
        """Serialise a message header as integers for writing to the network.

        Returns the serialised array.
        """
        array = _SerialisedMessageHeaderType()
        array[0]  = msg.msg.start_guard;
        array[1]  = msg.msg.id.network_id;
        array[2]  = msg.msg.id.serial_num;
        array[3]  = msg.msg.in_reply_to.network_id;
        array[4]  = msg.msg.in_reply_to.serial_num;
        array[5]  = msg.msg.to;
        array[6]  = msg.msg.from_;
        array[7]  = msg.msg.orig_from.network_id;
        array[8]  = msg.msg.orig_from.local_id;
        array[9]  = msg.msg.final_to.network_id;
        array[10] = msg.msg.final_to.local_id;
        array[11] = msg.msg.extra;                # to save adding it in the future
        array[12] = msg.msg.flags;
        array[13] = msg.msg.name_len;
        array[14] = msg.msg.data_len;
        # There's no point in sending the name and data pointers - since we must
        # be sending an "entire" message, they must be NULL, and anyway they're
        # pointers...
        array[15] = msg.msg.end_guard;

        for ii, item in enumerate(array):
            array[ii] = htonl(item)

        return array

    def unserialise_message_header(self, data):
        """Unserialise a message header from integers read from the network.

        Returns (name_len, data_len, array)
        """
        array = _struct_from_string(_SerialisedMessageHeaderType, data)
        for ii, item in enumerate(array):
            array[ii] = ntohl(array[ii])
        return array[13], array[14], array

    def read_message_from_socket(self):
        """Read a message from the other Limpet.

        Returns the corresponding Message instance.
        """

        # First, read the message header
        header = self.sock.recv(_SERIALISED_MESSAGE_HEADER_LEN*4,
                                socket.MSG_WAITALL)
        if header == '':
            raise OtherLimpetGoneAway()

        name_len, data_len, array = self.unserialise_message_header(header)

        if array[0] != Message.START_GUARD:
            raise GiveUp('Message data start guard is %08x,'
                         ' not %08x'%(array[0],Message.START_GUARD))

        if array[-1] != Message.END_GUARD:
            raise GiveUp('Message data end guard is %08x,'
                         ' not %08x'%(array[-1],Message.END_GUARD))

        name = self.sock.recv(calc_padded_name_len(name_len),
                              socket.MSG_WAITALL)
        if name == '':
            raise OtherLimpetGoneAway()

        if data_len:
            data = self.sock.recv(calc_padded_data_len(data_len),
                                  socket.MSG_WAITALL)
            if data == '':
                raise OtherLimpetGoneAway()
        else:
            data = None

        end = self.sock.recv(4, socket.MSG_WAITALL)
        value = struct.unpack('!L', end)   # unsigned long, network order
        end = ntohl(value[0])
        if end != Message.END_GUARD:
            raise GiveUp('Final message data end guard is %08x,'
                         ' not %08x'%(end,Message.END_GUARD))

        # We know enough to sort out the network order of the integers in
        # the Replier Bind Event's data
        if name[:name_len] == '$.KBUS.ReplierBindEvent':
            hdr = _struct_from_string(_ReplierBindEventHeader, data[:data_len])
            hdr.is_bind = ntohl(hdr.is_bind)
            hdr.binder  = ntohl(hdr.binder)
            rest = data[ctypes.sizeof(_ReplierBindEventHeader):data_len]
            # And just replace the original with the amended version 
            data=_struct_to_string(hdr)+rest
        
        return Message(name[:name_len],
                       data=data[:data_len] if data else None,
                       id=MessageId(array[1],array[2]),
                       in_reply_to=MessageId(array[3],array[4]),
                       to=array[5], from_=array[6],
                       orig_from=OrigFrom(array[7],array[8]),
                       final_to=OrigFrom(array[9],array[10]),
                       flags=array[12])

    def write_message_to_socket(self, msg):
        """Write a Message to the other Limpet.
        """
        # If KBUS gave us a message with an unset network id, then it is
        # a local message, and we set its network id to our own before we
        # pass it on. The combination of (network_id, local_id) should then
        # be unique across our whole network of Limpets/KBUSes.
        #
        # If KBUS gave us a message with a set network id, then we must
        # preserve it.
        #
        # For instance, we might well have a local message "called" [0:27], and
        # also receive a remote message called [130:27] -- we need to retain
        # these as distinct, so we can change the first (to have our own
        # "local" network id), but may not change the second.
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
            msg._orig_from.network_id = self.network_id
            msg._orig_from.local_id = msg.from_

        # We know enough to sort out the network order of the integers in
        # the Replier Bind Event's data
        if msg.name == '$.KBUS.ReplierBindEvent':
            hdr = _struct_from_string(_ReplierBindEventHeader, msg.data)
            hdr.is_bind = htonl(hdr.is_bind)
            hdr.binder  = htonl(hdr.binder)
            rest = msg.data[ctypes.sizeof(_ReplierBindEventHeader):]
            # And just replace the original with the amended version 
            msg = Message(msg, data=_struct_to_string(hdr)+rest)

        header = self.serialise_message_header(msg)
        self.sock.sendall(header)

        self.sock.sendall(msg.name)
        padded_name_len = calc_padded_name_len(msg.msg.name_len)
        if len(msg.name) != padded_name_len:
            self.sock.sendall('\0'*(padded_name_len - len(msg.name)))

        if msg.msg.data_len:
            self.sock.sendall(msg.data)
            padded_data_len = calc_padded_data_len(msg.msg.data_len)
            if len(msg.data) != padded_data_len:
                self.sock.sendall('\0'*(padded_data_len - len(msg.data)))

        end_guard = struct.pack('!L', header[-1])
        self.sock.sendall(end_guard)       # end guard again

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
        kbus_name   = 'KBUS%u'%self.ksock_id
        limpet_name = 'Limpet%d'%self.other_network_id
        kbus_to_us_hdr  = '%u %s->Us%s'%(self.network_id, kbus_name, ' '*(len(limpet_name)-2))
        nowt_to_limpet_hdr = '%u %s->%s'%(self.network_id, ' '*len(kbus_name), limpet_name)
        spaces_hdr = ' '*len(kbus_to_us_hdr)

        if self.verbosity > 1:
            print '%s %s'%(kbus_to_us_hdr, str(msg))

        if msg.name == '$.KBUS.ReplierBindEvent':
            # If this is the result of *us* binding as a replier (by proxy),
            # then we do *not* want to send it to the other Limpet!
            is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
            if binder_id == self.ksock_id:
                if self.verbosity > 1:
                    print '%s Which is us -- ignore'%(spaces_hdr)
                return

        if msg.is_request() and msg.wants_us_to_reply():
            # Remember the details of this Request for when we get a Reply
            # (Note that the message id itself is not suitable as a key,
            # as it is not immutable, and does not have a __hash__ method)
            key = (msg._id.network_id, msg._id.serial_num)
            self.our_requests[key] = (msg.from_, msg.to)

        if msg._id.network_id == self.other_network_id:
            # This is a message that originated with our pair Limpet (so it's
            # been from the other Limpet, to us, to KBUS, and we're now getting
            # it back again). Therefore we want to ignore it. When the original
            # message was sent to the other KBUS (before any Limpet touched
            # it), any listeners on that side would have heard it from that
            # KBUS, so we don't want to send it back to them yet again...
            if self.verbosity > 1:
                print '%s From the other Limpet -- ignore'%(spaces_hdr)
            return

        self.write_message_to_socket(msg)
        if self.verbosity > 1:
            # Write out the message with its amended detals
            print '%s %s'%(nowt_to_limpet_hdr, str(msg))

    def amend_reply_from_socket(self, hdr, msg):
        """Do whatever is necessary to a Reply from the other Limpet.

        Returns the amended message.
        """
        # If this message is in reply to a message from our network,
        # revert to the original message id
        if msg._in_reply_to.network_id == self.network_id:
            msg._in_reply_to.network_id = 0

        # Look up the original Request and amend appropriately
        key = (msg._in_reply_to.network_id, msg._in_reply_to.serial_num)
        try:
            from_, to = self.our_requests[key]
            del self.our_requests[key]          # we shouldn't see it again

            # What if it's a Status message? Essentially, we don't care,
            # since we still need to send it on anyway.

            # The simplest thing to do is just to create a new Reply
            # with the correct details
            msg = Reply(msg.name, data=msg.data,
                        in_reply_to=MessageId(key[0],key[1]),
                        to=from_, orig_from=msg.orig_from)
        except KeyError:
            # We already dealt with this Reply once, so this should not
            # happen (remember, we asked for only one copy of each message)
            if self.verbosity > 1:
                print '%s ignored as a "listen" copy'%(' '*len(hdr))
            raise

        if self.verbosity > 1:
            print '%s as %s'%(' '*(len(hdr)-3), str(msg))

        return msg

    def amend_request_from_socket(self, hdr, msg):
        """Do whatever is necessary to a Stateful Request from the other Limpet.

        Returns the amended message.
        """
        # The Request will have been marked "to" our Limpet pair
        # (otherwise we would not have received it).

        # If the 'final_to' has a network id that matches ours,
        # then we need to unset that, as it has clearly now come
        # into its "local" network.
        if self.verbosity > 1:
            print '%s *** final_to.network_id %u, network_id %u'%(hdr,
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
                print '%s *** There is no Replier - Replier gone away'%hdr
            error = Message('$.KBUS.Replier.GoneAway',
                            to=msg.from_,
                            in_reply_to=msg.id)
            self.write_message_to_socket(error)
            raise NoMessage

        if self.verbosity > 1:
            print '%s *** %s, kbus replier %u'%(hdr,
                  'Local' if is_local else 'Nonlocal',replier_id)

        if is_local:
            # The KBUS we're going to write the message to is
            # the final KBUS. Thus the replier id must match
            # that of the original Replier
            if replier_id != msg._final_to.local_id:
                # Oops - wrong replier - someone rebound
                if self.verbosity > 1:
                    print '%s *** Replier is %u, wanted %u - ' \
                          'Replier gone away'%(hdr,replier_id,msg._final_to.local_id)
                error = Message('$.KBUS.Replier.NotSameKsock', # XXX New message name
                                to=msg.from_,
                                in_reply_to=msg.id)
                self.write_message_to_socket(error)
                raise NoMessage

        # Regardless, we believe the message is OK, so need to
        # adjust who it is meant to go to (locally)
        if is_local:
            # If we're in our final stage, then we insist that the
            # Replier we deliver to be the Replier we expected
            msg.msg.to = msg._final_to.local_id
        else:
            # If we're just passing through, then just deliver it to
            # whoever is listening, on the assumption that they in turn
            # will pass it along, until it reaches its destination.
            # XXX What happens if they are not a Limpet?
            # XXX That would be bad - but I'm not sure how we could
            # XXX tell (short of allowing Limpets to register with
            # XXX KBUS, so that we can ask - and then a non-Limpet
            # XXX could presumably *pretend* to be a Limpet anyway)
            # XXX - a potentially infinite shell game then ensues...
            msg.msg.to = replier_id

        if self.verbosity > 1:
            print '%s Adjusted the msg.to field'%hdr

        return msg

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
        kbus_name  = 'KBUS%u'%self.ksock_id
        limpet_name = 'Limpet%d'%self.other_network_id
        limpet_to_us_hdr  = '%u %s->Us%s'%(self.network_id, limpet_name, ' '*(len(kbus_name)-2))
        nowt_to_kbus_hdr   = '%u %s->%s'%(self.network_id, ' '*len(limpet_name), kbus_name)
        spaces_hdr = ' '*len(limpet_to_us_hdr)

        if self.verbosity > 1:
            print '%s %s'%(limpet_to_us_hdr, str(msg))

        if msg.name == '$.KBUS.ReplierBindEvent':
            # We have to bind/unbind as a Replier in proxy
            is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
            if is_bind:
                if self.verbosity > 1:
                    print '%s BIND "%s'%(spaces_hdr,name)
                self.ksock.bind(name, True)
                self.replier_for[name] = binder_id
            else:
                if self.verbosity > 1:
                    print '%s UNBIND "%s'%(spaces_hdr,name)
                self.ksock.unbind(name, True)
                del self.replier_for[name]
            return

        if msg.is_reply():                   # a Reply (or Status)
            try:
                msg = self.amend_reply_from_socket(spaces_hdr, msg)
            except KeyError:
                return
        elif msg.is_stateful_request() and msg.wants_us_to_reply():
            try:
                msg = self.amend_request_from_socket(spaces_hdr, msg)
            except NoMessage:
                return

        try:
            if self.verbosity > 1:
                print '%s %s'%(nowt_to_kbus_hdr, str(msg))
            self.ksock.send_msg(msg)
        except IOError as exc:
            # If we were sending a Request, we need to fake an
            # appropriate Reply.
            if msg.is_request():
                try:
                    errname = '$.KBUS.RemoteError.%s'%errno.errorcode[exc.errno]
                except KeyError:
                    errname = '$.KBUS.RemoteError.%d'%exc.errno
                if self.verbosity > 1:
                    print '%s *** Remote error %s'%(nowt_to_kbus_hdr, errname)
                error = Message(errname, to=msg.from_, in_reply_to=msg.id)
                self.write_message_to_socket(error)
                return
            #
            # If we were sending a Reply, we need to think whether we
            # can do anything useful...
            #
            # XXX TODO
            print '%s send_msg: %s -- continuing'%(nowt_to_kbus_hdr, exc)


    def run_forever(self, termination_message):
        """Or until we're interrupted, or read the termination message from KBUS.

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
                    print '%u ---------------------- Message from KBUS'%self.network_id
                    msg = self.ksock.read_next_msg()
                    if msg.name == termination_message:
                        raise GiveUp('Termination requested via %s message'%termination_message)
                    self.handle_message_from_kbus(msg)

                if self.sock in r:
                    print '%u ---------------------- Message from other Limpet'%self.network_id
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

def connect_as_server(address, family, verbosity=1):
    """Connect to a socket as a server.

    We start listening, until we get someone connecting to us.

    Returns a tuple (listener_socket, connection_socket).
    """
    if verbosity > 1:
        print 'Listening on', address

    listener = socket.socket(family, socket.SOCK_STREAM)
    # Try to allow address reuse as soon as possible after we've finished
    # with it
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(address)

    listener.listen(1)
    connection, address = listener.accept()

    if verbosity:
        print 'Connection accepted from (%s, %s)'%(connection, address)

    return (listener, connection)

def connect_as_client(address, family, verbosity=1):
    """Connect to a socket as a client.

    Returns the socket.
    """
    if family == socket.AF_INET:
        sockname = '%s:%s'%address
    else:
        sockname = address

    try:
        sock = socket.socket(family, socket.SOCK_STREAM)
        sock.connect(address)

        if verbosity:
            print 'Connected to "%s" as client'%sockname

        return sock
    except Exception as exc:
        raise GiveUp('Unable to connect to "%s" as client: %s'%(sockname, exc))

def remove_socket_file(name):
    # Assuming this is an address representing a file in the filesystem,
    # delete it so we can use it again...
    try:
        os.remove(name)
    except Exception as err:
        raise GiveUp('Unable to delete socket file "%s": %s'%(name, err))

def run_a_limpet(is_server, address, family, kbus_device, network_id,
                 message_name='$.*', termination_message=None, verbosity=1):
    """Run a Limpet.

    A Limpet has two "ends":

    1. 'kbus_device' specifies which KBUS device it should communicate
       with, via ``ksock = Ksock(kbus_device, 'rw')``.

    2. 'socket_addresss' is the address for the socket used to
       communicate with its paired Limpet. This should generally be a
       path name (if communication is with another Limpet on the same
       machine, via Unix domain sockets), or a ``(host, port)`` tuple (for
       communication with a Limpet on another machine, via the internet).

    Messages received from KBUS get sent to the other Limpet.

    Messages sent from the other Limpet get sent to KBUS.

    - is_server is true if we are the "server" of the Limpet pair, false
      if we are the "client"
    - address is the socket address we use to talk to the other Limpet
    - family is AF_UNIX or AF_INET, determining what sort of address we
      want -- a pathname for the former, a <host>:<port> string for the
      latter
    - kbus_device is which KBUS device to open
    - network_id is the network id to set in message ids when we are
      forwarding a message to the other Limpet. It must be greater
      than zero.
    - message_name is the name of the message (presumably a wildcard)
      we are forwarding
    - if termination_message is not None, then we will stop when a message
      with that name is read from KBUS
    - if verbosity is 0, we don't output any "useful" messages, if it is
      1 we just announce ourselves, if it is 2 (or higher) we output
      information about each message as it is processed.
    """
    if family not in (socket.AF_UNIX, socket.AF_INET):
        raise ValueError('Socket family is %d, must be AF_UNIX (%s) or'
                         ' AF_INET (%d)'%(family,
                                          socket.AF_UNIX, socket.AF_INET))

    print 'Limpet: %s via %s for KBUS %d, using network id %d'%('server' if is_server else 'client',
            address, kbus_device, network_id)

    if is_server:
        listener, sock = connect_as_server(address, family, verbosity)
    else:
        sock = connect_as_client(address, family, verbosity)

    try:
        with Ksock(kbus_device, 'rw') as ksock:
            with Limpet(ksock, sock, network_id, message_name, verbosity) as l:
                if verbosity:
                    print l
                    if termination_message:
                        print "Terminate by sending a message named '%s'"%termination_message
                l.run_forever(termination_message)
    except Exception as exc:
        if verbosity > 1:
            print 'Closing socket'
        if not is_server:
            sock.shutdown(socket.SHUT_RDWR)
        sock.close()

        if is_server:
            if verbosity > 1:
                print 'Closing listener socket'
            listener.close()
            if family == socket.AF_UNIX:
                remove_socket_file(address)
        raise

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
