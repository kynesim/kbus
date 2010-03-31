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
# Portions created by the Initial Developer are Copyright (C) 2010
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Kynesim, Cambridge UK
#   Tibs <tibs@tonyibbs.co.uk>
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

class ErrorMessage(Exception):
    """Something went wrong trying to send a message to KBUS.

    There is an error message, for sending back to the other Limpet,
    in our ``.error`` value.
    """
    def __init__(self, error):
        self.error = error

class BadMessage(Exception):
    """We have read a badly formatted KBUS message.
    """
    pass

_SERIALISED_MESSAGE_HEADER_LEN = 16
_SerialisedMessageHeaderType = ctypes.c_uint32 * _SERIALISED_MESSAGE_HEADER_LEN

class LimpetKsock(Ksock):
    """A Limpet proxies KBUS messages to/from another Limpet.

    This class wraps itself around a Ksock, transforming messages as they are
    read from the Ksock, or written to it.
    """

    def __init__(self, which, our_network_id, their_network_id,
                 message_name='$.*', verbosity=1, termination_message=None):
        """A Limpet has two "ends":

        1. At one end, there is a Ksock, specifying which KBUS device it
           should communicate with. This will be open for read and write.
           The message transformer needs to know which Ksock the Limpet
           is connected to.
        2. At the other end, there is (some sort of communication link to)
           another Limpet, which is connected to its own Ksock on another KBUS
           device. This class is not concerned with the details of that
           communication,

        The idea is that:

        * Messages received from our Ksock get sent to the other Limpet.
        * Messages sent from the other Limpet get sent to our ksock.

        - which is the Ksock device number (as in ``/dev/kbus<which>``). This
          will always be opened for both read and write.
        - our_network_id is the network id which identifies this Limpet. It is
          set in message ids when we are forwarding a message to the other
          Limpet. It must be greater than zero.
        - their_network_id is the network if of the other Limpet. It must not
          be the same as our_network_id. It must be greater than zero.
        - message_name is the message name that this Limpet will bind to, and
          forward. This will normally be a wildcard, and defaults to "$.*".
          Other messages will not be read.
        - if verbosity is 0, we don't output any "useful" messages, if it is
          1 we just announce ourselves, if it is 2 (or higher) we output
          information about each message as it is processed.
        - if termination_message is non-None, then when we read this message,
          the reading method will raise GiveUp.
        """
        if our_network_id < 1:
            raise ValueError('Our Limpet network id must be > 0, not'
                             ' %d'%our_network_id)
        if their_network_id < 1:
            raise ValueError('Their Limpet network id must be > 0, not'
                             ' %d'%their_network_id)
        if our_network_id == their_network_id:
            raise ValueError('Our Limpet network id (%d) must be different'
                             ' than theirs'%our_network_id)

        super(LimpetKsock, self).__init__(which, 'rw')

        self.network_id = our_network_id
        self.other_network_id = their_network_id
        self.message_name = message_name
        self.verbosity = verbosity
        self.termination_message = termination_message

        self._ksock_id = self.ksock_id()

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
            if verbosity > 1:
                self.kernel_module_verbose(True)

            # Note that we only want one copy of a message, even if we were
            # registered as (for instance) both Replier and Listener
            super(LimpetKsock, self).want_messages_once(True)

            # Bind to proxy the requested message name
            super(LimpetKsock, self).bind(self.message_name)

            # We always want to bind for Replier Bind Event messages, as well
            # - since we're only going to get one copy of each message, it is
            # safe to bind to this again, even if the ``message_name`` has
            # implicitly already done that
            super(LimpetKsock, self).bind('$.KBUS.ReplierBindEvent')

            # And ask KBUS to *send* such messages
            super(LimpetKsock, self).report_replier_binds(True)
        except:
            self.close()
            raise

    def __str__(self):
        if self.fd:
            return 'Limpet wrapper on device %d, id %d, network id %d, for "%s"'%(self.which,
                    self._ksock_id, self.network_id, self.message_name)
        else:
            return 'Limpet wrapper on device %d (closed)'%(self.which)

    def __repr__(self):
        if self.fd:
            return '<Limpet on %s, id %d, network id %s open>'%(self.name,
                    self._ksock_id, self.network_id)
        else:
            return '<Limpet on %s closed>'%(self.name)

    def bind(self, *args):
        """Not meaningful for this class."""
        raise NotImplemented('bind method is not relevant to LimpetKsock')

    def unbind(self, *args):
        """Not meaningful for this class."""
        raise NotImplemented('unbind method is not relevant to LimpetKsock')

    def len_left(self):
        """Not meaningful for this class.

        We only support reading an entire message in one go.
        """
        raise NotImplemented('len_left method is not relevant to LimpetKsock')

    def discard(self):
        """Not meaningful for this class.

        We only support reading an entire message in one go.
        """
        raise NotImplemented('discard method is not relevant to LimpetKsock')

    def want_messages_once(self, only_once=False, just_ask=False):
        """Not meaningful for this class.

        We require that this be set, and do not want the user to change it
        """
        raise NotImplemented('want_messages_once method is not relevant to LimpetKsock')

    def report_replier_binds(self, report_events=True, just_ask=False):
        """Not meaningful for this class.

        We require that this be set, and do not want the user to change it
        """
        raise NotImplemented('report_replier_binds method is not relevant to LimpetKsock')

    def write_msg(self, message):
        """Not meaningful for this class.

        We only support writing and sending an entire message in one go.
        """
        raise NotImplemented('write_msg method is not relevant to LimpetKsock')

    def send_msg(self, message):
        """Write a Message (from the other Limpet) to our Ksock, and send it.

        Entirely equivalent to calling 'write_msg' and then 'send',
        and returns the MessageId of the sent message, as 'send' does.

        If the message was one we need to ignore (i.e., we're not interested
        in sending it), raises NoMessage.

        If we need to send a message back to the other Limpet, then that
        exception will have the messsage as its argument.
        """
        message = self._handle_message_from_socket(message)
        if message is None:
            raise NoMessage()

        super(LimpetKsock, self).write_msg(message)
        return super(LimpetKsock, self).send()

    def write_data(self, data):
        """Not meaningful for this class.

        We only support writing an entire message in one go.
        """
        raise NotImplemented('write_data method is not relevant to LimpetKsock')

    def read_msg(self, length):
        """Read a Message of length 'length' bytes.

        It is assumed that 'length' was returned by a previous call
        of 'next_msg'. It must be large enough to cause the entire
        message to be read.

        After the data has been read, it is passed to Message to
        construct a message instance, which is returned.

        Returns None if there was nothing to be read, or if the message
        read is one that this Limpet should ignore.
        """
        message = super(LimpetKsock, self).read_msg(length)
        if message is None:
            return None

        if message.name == self.termination_message:
            raise GiveUp('Received termination message %s to %s'%(
                self.termination_message,str(self)))

        message = self._handle_message_from_kbus(message)
        if message is None:
            return None

        return message

    def read_next_msg(self):
        """Read the next Message.

        Equivalent to a call of 'next_msg', followed by reading the
        appropriate number of bytes and passing that to Message to
        construct a message instance, which is returned.

        Returns None if there was nothing to be read, or if the message
        read is one that this Limpet should ignore.
        """
        message = super(LimpetKsock, self).read_next_msg()
        if message is None:
            return None

        print '>>> message %s'%message
        print '>>> termina %s'%self.termination_message

        if message.name == self.termination_message:
            raise GiveUp('Received termination message %s to %s'%(
                self.termination_message,str(self)))

        message = self._handle_message_from_kbus(message)
        if message is None:
            return None

        return message

    def read_data(self, count):
        """Not meaningful for this class.

        We only support reading an entire message in one go.
        """
        raise NotImplemented('read_data method is not relevant to LimpetKsock')

    def _sort_out_network_ids(self, msg):
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


    def _handle_message_from_kbus(self, msg):
        """Do the appropriate thing with a message from KBUS.

        We take a message read from KBUS, and get it ready to write it to the
        other Limpet.

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

        Returns the amended message, or None if the message is to be ignored.
        """
        kbus_name   = 'KBUS%u'%self._ksock_id
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
            if binder_id == self._ksock_id:
                if self.verbosity > 1:
                    print '%s Which is us -- ignore'%(spaces_hdr)
                return None

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
            return None

        self._sort_out_network_ids(msg)

        return msg

    def _amend_reply_from_socket(self, hdr, msg):
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

    def _amend_request_from_socket(self, hdr, msg):
        """Do whatever is necessary to a Stateful Request from the other Limpet.

        Returns the amended message, or raises ErrorMessage(<error message>),
        where <error message> is appropriate for sending to the other
        Limpet to report the problem.
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
        replier_id = self.find_replier(msg.name)
        if replier_id is None:
            # Oh dear - there is no replier
            if self.verbosity > 1:
                print '%s *** There is no Replier - Replier gone away'%hdr
            error = Message('$.KBUS.Replier.GoneAway',
                            to=msg.from_,
                            in_reply_to=msg.id)
            raise ErrorMessage(error)

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
                raise ErrorMessage(error)

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

    def _handle_message_from_socket(self, msg):
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

        Returns the amended message, or None if it should be ignored.

        Raises ErrorMessage(<error message>) if we should send <error message> to
        the other Limpet.
        """
        kbus_name  = 'KBUS%u'%self._ksock_id
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
                super(LimpetKsock, self).bind(name, True)
                self.replier_for[name] = binder_id
            else:
                if self.verbosity > 1:
                    print '%s UNBIND "%s'%(spaces_hdr,name)
                super(LimpetKsock, self).unbind(name, True)
                del self.replier_for[name]
            return None

        if msg.is_reply():                   # a Reply (or Status)
            try:
                msg = self._amend_reply_from_socket(spaces_hdr, msg)
            except KeyError:
                return None
        elif msg.is_stateful_request() and msg.wants_us_to_reply():
            msg = self._amend_request_from_socket(spaces_hdr, msg)

        return msg

    def could_not_send_to_kbus_msg(self, msg, exc):
        """If a send to our Ksock failed, call this to generate a message.

        'msg' is the message we tried to send.

        'exc' is the IOError exception that was raised by the failed call of
        'send_msg()'

        We return an appropriate message to send to the other Limpet, or None
        if we could not determine one.
        """
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
            self.write_message_to_other_limpet(error)
            return error
        #
        # If we were sending a Reply, we need to think whether we
        # can do anything useful...
        #
        # XXX TODO
        print '%u send_msg: %s -- continuing'%(self.network_id, exc)
        return None


def serialise_message_header(msg):
    """Serialise a message header as integers for writing to the network.

    Does not touch the message data in any way.

    Returns the serialised array. Note that this omits the name pointer and
    data pointer fields.
    """
    array = _SerialisedMessageHeaderType()
    array[0]  = msg.msg.start_guard
    array[1]  = msg.msg.id.network_id
    array[2]  = msg.msg.id.serial_num
    array[3]  = msg.msg.in_reply_to.network_id
    array[4]  = msg.msg.in_reply_to.serial_num
    array[5]  = msg.msg.to
    array[6]  = msg.msg.from_
    array[7]  = msg.msg.orig_from.network_id
    array[8]  = msg.msg.orig_from.local_id
    array[9]  = msg.msg.final_to.network_id
    array[10] = msg.msg.final_to.local_id
    array[11] = msg.msg.extra                # to save adding it in the future
    array[12] = msg.msg.flags
    array[13] = msg.msg.name_len
    array[14] = msg.msg.data_len
    # There's no point in sending the name and data pointers - since we must
    # be sending an "entire" message, they must be NULL, and anyway they're
    # pointers...
    array[15] = msg.msg.end_guard

    for ii, item in enumerate(array):
        array[ii] = htonl(item)

    return array

def unserialise_message_header(data):
    """Unserialise a message header from integers read from the network.

    This should be an array equivalent to that returned by
    serialise_message_header() (in particular, omitting the name and data
    pointer fields).

    Returns (name_len, data_len, array)
    """
    array = _struct_from_string(_SerialisedMessageHeaderType, data)
    for ii, item in enumerate(array):
        array[ii] = ntohl(array[ii])
    return array[13], array[14], array

def convert_ReplierBindEvent_data_from_network(data, data_len):
    """Given the data for a ReplierBindEvent, convert it from network order.

    Returns a new version of the data, converted.
    """
    hdr = _struct_from_string(_ReplierBindEventHeader, data[:data_len])
    hdr.is_bind  = ntohl(hdr.is_bind)
    hdr.binder   = ntohl(hdr.binder)
    hdr.name_len = ntohl(hdr.name_len)
    rest = data[ctypes.sizeof(_ReplierBindEventHeader):data_len]
    return _struct_to_string(hdr)+rest

def convert_ReplierBindEvent_data_to_network(data):
    """Given the data for a ReplierBindEvent, convert it to network order.

    Returns a new version of the data, converted.
    """
    hdr = _struct_from_string(_ReplierBindEventHeader, data)
    hdr.is_bind  = htonl(hdr.is_bind)
    hdr.binder   = htonl(hdr.binder)
    hdr.name_len = htonl(hdr.name_len)
    rest = data[ctypes.sizeof(_ReplierBindEventHeader):]
    return _struct_to_string(hdr)+rest


class LimpetExample(object):
    """A Limpet proxies KBUS messages to/from another Limpet.

    This is a simple example of how a Limpet might be implemented.

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

    def __init__(self, which, sock, network_id, message_name='$.*', verbosity=1,
                 termination_message=None):
        """A Limpet has two "ends":

        1. 'which' specifies which KBUS device it should communicate
           with (i.e., ``/dev/kbus<which>``)
        2. 'sock' is the socket used to communicate with its paired Limpet.

        Messages received from KBUS get sent to the other Limpet.

        Messages sent from the other Limpet get sent to KBUS.

        - which is the KBUS device number
        - sock is the socket to the other Limpet
        - network_id is the network id to set in message ids when we are
          forwarding a message to the other Limpet. It must be greater
          than zero.
        - message_name is the name of the message (presumably a wildcard)
          we are forwarding
        - if verbosity is 0, we don't output any "useful" messages, if it is
          1 we just announce ourselves, if it is 2 (or higher) we output
          information about each message as it is processed.
        - if termination_message is non-None, then when we read this message,
          the reading method will raise GiveUp.
        """
        if network_id < 1:
            raise ValueError('Limpet network id must be > 0, not %d'%network_id)

        self.sock = sock
        self.verbosity = verbosity

        # We don't know the network id of our Limpet pair yet
        self.other_network_id = None

        # A dictionary of { <message_name> : <binder_id> } of the messages
        # we are bound as a "Replier in proxy" for.
        self.replier_for = {}

        # A dictionary of information about each Request that we have proxied
        # as a Replier for that Request. We remember the Request message id as
        # the key, and the from/to information as the data
        self.our_requests = {}

        # So we're set up to talk at both ends - now sort out what we're
        # talking about

        # Swap network ids with our pair
        self._send_network_id(network_id)
        other_network_id = self._read_network_id()

        if other_network_id == network_id:
            # We rather rely on unique network ids, and *in particular*
            # that this pair have different ids
            raise GiveUp('This Limpet and its pair both have'
                         ' network id %d'%network_id)
        if self.verbosity > 1:
            print 'Other Limpet has network id',other_network_id

        self.wrapper = LimpetKsock(which, network_id, other_network_id,
                                   message_name, verbosity, termination_message)
        self.ksock_id = self.wrapper.ksock_id()

    def _send_network_id(self, network_id):
        """Send our pair Limpet our network id.
        """
        self.sock.sendall('HELO')
        data = struct.pack('!L', network_id)   # unsigned long, network order
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
        return struct.unpack('!L', data)[0]   # unsigned long, network order


    def close(self):
        """Tidy up when we're finished.
        """
        if self.verbosity:
            print 'Limpet closed'

    def __repr__(self):
        sf = {socket.AF_INET:'socket.AF_INET',
              socket.AF_UNIX:'socket.AF_UNIX'}
        parts = []
        parts.append('ksock=%s'%self.wrapper)
        parts.append('sock=%s'%self.sock)
        parts.append('network_id=%d'%self.wrapper.network_id)
        if self.message_name != '$.*':
            parts.append('message_name=%s'%repr(self.message_name))
        if self.verbosity != 1:
            parts.append('verbosity=%s'%self.verbosity)

        return 'Limpet(%s)'%(', '.join(parts))

    def __str__(self):
        return 'Limpet from KBUS Ksock %u via socket %s'%(self.ksock_id,
                                                          self.sock)

    def read_message_from_other_limpet(self):
        """Read a message from the other Limpet.

        Returns the corresponding Message instance.
        """

        # First, read the message header
        header = self.sock.recv(_SERIALISED_MESSAGE_HEADER_LEN*4,
                                socket.MSG_WAITALL)
        if header == '':
            raise OtherLimpetGoneAway()

        name_len, data_len, array = unserialise_message_header(header)

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
        #value = struct.unpack('!L', end)   # unsigned long, network order
        #end = ntohl(value[0])
        end = struct.unpack('!L', end)[0]   # unsigned long, network order
        if end != Message.END_GUARD:
            raise GiveUp('Final message data end guard is %08x,'
                         ' not %08x'%(end,Message.END_GUARD))

        # We know enough to sort out the network order of the integers in
        # the Replier Bind Event's data
        if name[:name_len] == '$.KBUS.ReplierBindEvent':
            data = convert_ReplierBindEvent_data_from_network(data, data_len)
        
        return Message(name[:name_len],
                       data=data[:data_len] if data else None,
                       id=MessageId(array[1],array[2]),
                       in_reply_to=MessageId(array[3],array[4]),
                       to=array[5], from_=array[6],
                       orig_from=OrigFrom(array[7],array[8]),
                       final_to=OrigFrom(array[9],array[10]),
                       flags=array[12])

    def write_message_to_other_limpet(self, msg):
        """Write a Message to the other Limpet.
        """
        # We know enough to sort out the network order of the integers in
        # the Replier Bind Event's data
        if msg.name == '$.KBUS.ReplierBindEvent':
            data = convert_ReplierBindEvent_data_to_network(msg.data)
            msg = Message(msg, data=data)

        header = serialise_message_header(msg)
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

        ##end_guard = struct.pack('!L', header[-1])
        end_guard = struct.pack('!L', Message.END_GUARD)
        self.sock.sendall(end_guard)       # end guard again

    def run_forever(self):
        """Or until we're interrupted, or read the termination message from KBUS.

        If an exception is raised, then the Limpet is closed as the method
        is exited.
        """
        try:
            while 1:
                # Wait for a message written to us, with no timeout
                # (at least for the moment)
                (r, w, x) = select.select( [self.wrapper, self.sock], [], [])

                if self.verbosity > 1:
                    print

                if self.wrapper in r:
                    print '%u ---------------------- Message from KBUS'% \
                            self.wrapper.network_id
                    msg = self.wrapper.read_next_msg()
                    if msg is not None:
                        self.write_message_to_other_limpet(msg)

                if self.sock in r:
                    print '%u ---------------------- Message from other Limpet'% \
                            self.wrapper.network_id
                    msg = self.read_message_from_other_limpet()
                    print '%u %s'%(self.wrapper.network_id,msg)
                    try:
                        msg_id = self.wrapper.send_msg(msg)
                        print '%u msg_id %s'%(self.wrapper.network_id,msg_id)
                    except NoMessage as exc:
                        # It turned out to be a message we should ignore - do so
                        print '%u IGNORED %s'%(self.wrapper.network_id,msg)
                    except ErrorMessage as exc:
                        self.write_message_to_other_limpet(exc.error)
                    except IOError as exc:
                        error = self.wrapper.could_not_send_to_kbus_msg(msg, exc)
                        if error is not None:
                            self.write_message_to_other_limpet(error)
                            return
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

    print 'Python Limpet: %s via %s for KBUS %d,' \
          ' using network id %d'%('Server' if is_server else 'Client',
                                  address, kbus_device, network_id)

    if is_server:
        listener, sock = connect_as_server(address, family, verbosity)
    else:
        sock = connect_as_client(address, family, verbosity)

    try:
        # The proposed new mechanism
        with LimpetExample(kbus_device, sock, network_id, message_name, verbosity,
                     termination_message) as l:
            if verbosity:
                print l
                if termination_message:
                    print "Terminate by sending a message named '%s'"%termination_message
            l.run_forever()
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
