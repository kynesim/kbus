"""Python code for using the kbus kernel module.

On Ubuntu, if I want ordinary users (in the admin group) to be able to
read/write '/dev/kbus0' then I need to have a file
'/etc/udev/rules.d/45-kbus.rules' which contains::

    KERNEL=="kbus[0-9]*",  MODE="0666", GROUP="admin"

Other operating systems will have other mechanisms, and on an embedded system
it is likely enough not to do this, as the "user" will be root.
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

from __future__ import with_statement

import fcntl
import ctypes
import array
import itertools

# Kernel definitions for ioctl commands
# Following closely from #include <asm[-generic]/ioctl.h>
# (and with some thanks to http://wiki.maemo.org/Programming_FM_radio/)
_IOC_NRBITS   = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14
_IOC_DIRBITS  = 2

_IOC_NRSHIFT   = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT  = _IOC_SIZESHIFT + _IOC_SIZEBITS

_IOC_NONE  = 0
_IOC_WRITE = 1
_IOC_READ  = 2

# Mustn't use "type" as an argument, since Python already has it...
def _IOC(d,t,nr,size):
    return ((d << _IOC_DIRSHIFT) | (ord(t) << _IOC_TYPESHIFT) | 
            (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT))
def _IO(t,nr):
    return _IOC(_IOC_NONE, t, nr, 0)
def _IOW(t,nr,size):
    return _IOC(_IOC_WRITE, t, nr, size)
def _IOR(t,nr,size):
    return _IOC(_IOC_READ, t, nr, size)
def _IOWR(t,nr,size):
    return _IOC(_IOC_READ | _IOC_WRITE, t, nr, size)

def _BIT(nr):
    return 1L << nr

def _set_bit(value,which):
    """Return 'value' with the bit 'which' set.

    'which' should be something like _BIT(3).
    """
    return value | which

def _clear_bit(value,which):
    """Return 'value' with the bit 'which' cleared.

    'which' should be something like _BIT(3).
    """
    if value & which:
        mask = ~which
        value = value & mask
    return value


class MessageId(ctypes.Structure):
    """A wrapper around a message id.

        >>> a = MessageId(1,2)
        >>> a
        MessageId(1,2)
        >>> a < MessageId(2,2) and a < MessageId(1,3)
        True
        >>> a == MessageId(1,2)
        True
        >>> a > MessageId(0,2) and a > MessageId(1,1)
        True

    We support addition in a limited manner:

        >>> a + 3
        MessageId(1,5)

    simply to make it convenient to generate unique message ids.
    """
    _fields_ = [('network_id', ctypes.c_uint32),
                ('serial_num', ctypes.c_uint32)]

    def __repr__(self):
        return 'MessageId(%u,%u)'%(self.network_id,self.serial_num)

    def _short_str(self):
        """For use in message structure reporting
        """
        return '%u:%u'%(self.network_id,self.serial_num)

    def __cmp__(self,other):
        if not isinstance(other,MessageId):
            return NotImplemented
        if self.network_id == other.network_id:
            if self.serial_num == other.serial_num:
                return 0
            elif self.serial_num < other.serial_num:
                return -1
            else:
                return 1
        elif self.network_id < other.network_id:
            return -1
        else:
            return 1

    def __add__(self,other):
        if not isinstance(other,int):
            return NotImplemented
        else:
            return MessageId(self.network_id,self.serial_num+other)

class Message(object):
    """A wrapper for a KBUS message

    A Message can be created in a variety of ways. Perhaps most obviously:

        >>> msg = Message('$.Fred')
        >>> msg
        Message('$.Fred', data=(), to=0L, from_=0L, in_reply_to=None, flags=0x00000000, id=None)

        >>> msg = Message('$.Fred', (0x1234,))
        >>> msg
        Message('$.Fred', data=(0x1234,), to=0L, from_=0L, in_reply_to=None, flags=0x00000000, id=None)

        >>> msg = Message('$.Fred', (0x1234,0xFFFF00FF))
        >>> msg
        Message('$.Fred', data=(0x1234, 0xffff00ff), to=0L, from_=0L, in_reply_to=None, flags=0x00000000, id=None)

        >>> msg1 = Message('$.Fred',data=(0x1234,))
        >>> msg1
        Message('$.Fred', data=(0x1234,), to=0L, from_=0L, in_reply_to=None, flags=0x00000000, id=None)

    Yes, that's a tuple as the "data" argument. It should be either None or a
    tuple of unsigned 32-bit integers.

    A Message can be constructed from another message directly:

        >>> msg2 = Message(msg1)
        >>> msg2 == msg1
        True

    or from the '.extract()' tuple:

        >>> msg3 = Message(msg1.extract())
        >>> msg3 == msg1
        True

    or from an equivalent list::

        >>> msg3 = Message(list(msg1.extract()))
        >>> msg3 == msg1
        True

    or one can use a "string" -- for instance, as returned by the KSock 'read'
    method:

        >>> msg_as_string = msg1.to_string()
        >>> msg4 = Message(msg_as_string)
        >>> msg4 == msg1
        True

    When constructing a message from another message, one may override
    particular values (but not the name):

        >>> msg5 = Message(msg1,to=9,in_reply_to=MessageId(0,3))
        >>> msg5
        Message('$.Fred', data=(0x1234,), to=9L, from_=0L, in_reply_to=MessageId(0,3), flags=0x00000000, id=None)

        >>> msg5a = Message(msg1,to=9,in_reply_to=MessageId(0,3))
        >>> msg5a == msg5
        True

    However, whilst it is possible to set (for instance) 'to' back to 0 by this method:

        >>> msg6 = Message(msg5,to=0)
        >>> msg6
        Message('$.Fred', data=(0x1234,), to=0L, from_=0L, in_reply_to=MessageId(0,3), flags=0x00000000, id=None)

    (and the same for any of the integer fields), it is not possible to set any
    of the message id fields to None:

        >>> msg6 = Message(msg5,in_reply_to=None)
        >>> msg6
        Message('$.Fred', data=(0x1234,), to=9L, from_=0L, in_reply_to=MessageId(0,3), flags=0x00000000, id=None)

    If you need to do that, go via the 'extract()' method:

        >>> (id,in_reply_to,to,from_,flags,name,data) = msg5.extract()
        >>> msg6 = Message(name,data,to,from_,None,flags,id)
        >>> msg6
        Message('$.Fred', data=(0x1234,), to=9L, from_=0L, in_reply_to=None, flags=0x00000000, id=None)

    For convenience, the parts of a Message may be retrieved as properties:

        >>> print msg1.id
        None
        >>> msg1.name
        '$.Fred'
        >>> msg1.to
        0L
        >>> msg1.from_
        0L
        >>> print msg1.in_reply_to
        None
        >>> msg1.flags
        0L
        >>> msg1.data[0]
        4660L
        >>> msg1.data[:]
        [4660L]
        >>> tuple(msg1.data)
        (4660L,)

    Message ids are objects if set:

        >>> msg1 = Message('$.Fred',data=(0x1234,),id=MessageId(0,33))
        >>> msg1
        Message('$.Fred', data=(0x1234,), to=0L, from_=0L, in_reply_to=None, flags=0x00000000, id=MessageId(0,33))
        >>> msg1.id
        MessageId(0,33)

    The arguments to Message() are:

    - 'arg' -- this is the initial argument, and is a message name (a string
      that starts '$.'), a Message, some data that may be interpreted as a
      message (e.g., an array.array of unsigned 32-bit words, or a string of
      the right form), or a tuple from the .extract() method of another
      Message.

    If 'arg' is a message name, then the keyword arguments may be used (if
    'arg' is not a messsage name, they will be ignored):

    - 'data' is data for the Message, either None or a tuple of unsigned 32-bit
      integers, or an array of such, or a string that can be converted to such
    - 'to' is the KSock id for the destination, for use in replies or in
      stateful messaging. Normally it should be left 0.
    - 'from_' is the KSock id of the sender. Normally this should be left
      0, as it is assigned by KBUS.
    - if 'in_reply_to' is non-zero, then it is the KSock id to which the
      reply shall go (taken from the 'from_' field in the original message).
      Setting 'in_reply_to' non-zero indicates that the Message *is* a reply.
      See also the Reply class, and especially the 'reply_to' function, which
      makes constructing replies simpler.
    - 'flags' can be used to set the flags for the message. If all that is
      wanted is to set Messages.WANT_A_REPLY flag, it is simpler to use the
      Request class to construct the message.
    - 'id' may be used to set the message id, although unless the network_id is
      set, KBUS will ignore this and set the id internally (this can be useful
      when constructing a message to compare received messages against).

    Our internal values are:

    - 'msg', which is the actual message data, as a KbusEntireMessageStruct
    - 'size', which is the size of that datastructure in bytes
    """

    START_GUARD = 0x7375626B
    END_GUARD   = 0x6B627573

    WANT_A_REPLY        = _BIT(0)
    WANT_YOU_TO_REPLY   = _BIT(1)
    SYNTHETIC           = _BIT(2)
    URGENT              = _BIT(3)

    ALL_OR_WAIT         = _BIT(8)
    ALL_OR_FAIL         = _BIT(9)

    # Header offsets (in case I change them again)
    IDX_START_GUARD            = 0
    IDX_ID_NETWORK_ID          = 1
    IDX_ID_SERIAL_NUM          = 2
    IDX_IN_REPLY_TO_NETWORK_ID = 3
    IDX_IN_REPLY_TO_SERIAL_NUM = 4
    IDX_TO                     = 5
    IDX_FROM                   = 6
    IDX_FLAGS                  = 7
    IDX_NAME_LEN               = 8
    IDX_DATA_LEN               = 9     # required to be the last fixed item
    IDX_END_GUARD              = -1

    def __init__(self, arg, data=None, to=None, from_=None, in_reply_to=None, flags=None, id=None):
        """Initialise a Message.

        All named arguments are meant to be "unset" by default.
        """

        if data:
            if isinstance(data,str):
                d = array.array('L',data)
                data = tuple(d.tolist())
            elif isinstance(data,array.array):
                if data.typecode != 'L':
                    raise ValueError("Message 'data' is an array.array('%c'), not 'L'"%\
                                     data.typecode)
                data = tuple(data.tolist())

        if isinstance(arg,Message):
            self._merge_args(arg.extract(),data,to,from_,in_reply_to,flags,id)
        elif isinstance(arg,tuple) or isinstance(arg,list):
            # A tuple from .extract(), or an equivalent tuple/list
            if len(arg) != 7:
                raise ValueError("Tuple arg to Message() must have"
                        " 7 values, not %d"%len(arg))
            else:
                self._merge_args(arg,data,to,from_,in_reply_to,flags,id)
        elif isinstance(arg,str) and arg.startswith('$.'):
            # It looks like a message name
            name = arg
            self._from_data(name,data,to,from_,in_reply_to,flags,id)
        elif arg and data is None and to is None and from_ is None and \
                in_reply_to is None and flags is None and id is None:
                # Assume it's sensible data...
                # (is this only allowed to be a "string"?)
                self.msg = entire_message_from_string(arg)
        else:
            raise ValueError,'Argument %s does not seem to make sense'%repr(arg)

        # Make sure the result *looks* like a message
        self._check()

        # And I personally find it useful to have the length available
        self.size = ctypes.sizeof(self.msg)

    def _merge_args(self,extracted, this_data, this_to, this_from_,
                    this_in_reply_to, this_flags, this_id):
        """Set our data from a msg.extract() tuple and optional arguments.

        Note that, if given, 'id' and 'in_reply_to' must be MessageId
        instances.

        Note that 'data' must be:

        1. an array.array('L',...) instance, or
        2. a string, or something else compatible, which will be converted to
           the above, or
        3. None.
        """
        (id,in_reply_to,to,from_,flags,name,data) = extracted
        if this_data        is not None: data        = this_data
        if this_to          is not None: to          = this_to
        if this_from_       is not None: from_       = this_from_
        if this_in_reply_to is not None: in_reply_to = this_in_reply_to
        if this_flags       is not None: flags       = this_flags
        if this_id          is not None: id          = this_id
        self._from_data(name, data, to, from_, in_reply_to, flags, id)

    def _from_data(self, name, data, to, from_, in_reply_to, flags, id):
        """Set our data from individual arguments.

        Note that, if given, 'id' and 'in_reply_to' must be MessageId
        instances.

        Note that 'data' must be a tuple of (unsigned, 32-bit) integers, or
        None.
        """
                                         
        if id:
            id_tuple = (id.network_id, id.serial_num)
        else:
            id_tuple = (0, 0)

        if in_reply_to:
            in_reply_to_tuple = (in_reply_to.network_id, in_reply_to.serial_num)
        else:
            in_reply_to_tuple = (0, 0)

        if not to:
            to = 0

        if not from_:
            from_ = 0

        if not flags:
            flags = 0

        if data is None:
            data = ()

        self.msg = entire_message_from_parts(id_tuple, in_reply_to_tuple,
                                             to, from_, flags, name, data)

    def _check(self):
        """Perform some basic sanity checks on our data.
        """
        # XXX Make the reporting of problems nicer for the user!
        assert self.msg.start_guard == self.START_GUARD
        assert self.msg.end_guard == self.END_GUARD
        if self.msg.name_len < 3:
            raise ValueError("Message name is %d long, minimum is 3"
                             " (e.g., '$.*')"%name_len_bytes)

    def __repr__(self):
        (id,in_reply_to,to,from_,flags,name,data) = self.extract()
        args = [repr(name),
                'data=%s'%_int_tuple_as_str(data),
                'to='+repr(to),
                'from_='+repr(from_),
                'in_reply_to='+repr(in_reply_to),
                'flags=0x%08x'%flags,
                'id='+repr(id)]
        return 'Message(%s)'%(', '.join(args))

    def __eq__(self,other):
        if not isinstance(other,Message):
            return False
        else:
            return (self.msg == other.msg)

    def __ne__(self,other):
        if not isinstance(other,Message):
            return True
        else:
            return (self.msg != other.msg)

    def equivalent(self,other):
        """Returns true if the two messages are mostly the same.

        For purposes of this comparison, we ignore:

        * 'id',
        * 'flags',
        * 'in_reply_to' and
        * 'from'
        """
        return (self.msg.to       == other.msg.to and
                self.msg.name_len == other.msg.name_len and
                self.msg.data_len == other.msg.data_len and
                self.msg.name     == other.msg.name and
                self.msg._data_eq(other.msg))

    def set_want_reply(self,value=True):
        """Set or unset the 'we want a reply' flag.
        """
        if value:
            self.msg.flags = _set_bit(self.msg.flags, Message.WANT_A_REPLY)
        else:
            self.msg.flags = _clear_bit(self.msg.flags, Message.WANT_A_REPLY)

    def set_urgent(self,value=True):
        """Set or unset the 'urgent message' flag.
        """
        if value:
            self.msg.flags = _set_bit(self.msg.flags, Message.URGENT)
        else:
            self.msg.flags = _clear_bit(self.msg.flags, Message.URGENT)

    def wants_us_to_reply(self):
        """Return true if we (*specifically* us) are should reply to this message.
        """
        return self.msg.flags & Message.WANT_YOU_TO_REPLY

    def is_synthetic(self):
        """Return true if this is a synthetic message - one generated by KBUS.
        """
        return self.msg.flags & Message.SYNTHETIC

    def is_urgent(self):
        """Return true if this is an urgent message.
        """
        return self.msg.flags & Message.URGENT

    def _get_id(self):
        network_id = self.msg.id.network_id
        serial_num = self.msg.id.serial_num
        if network_id == 0 and serial_num == 0:
            return None
        else:
            return MessageId(network_id,serial_num)

    def _get_in_reply_to(self):
        network_id = self.msg.in_reply_to.network_id
        serial_num = self.msg.in_reply_to.serial_num
        if network_id == 0 and serial_num == 0:
            return None
        else:
            return MessageId(network_id,serial_num)

    def _get_to(self):
        return self.msg.to

    def _get_from(self):
        return self.msg.from_

    def _get_flags(self):
        return self.msg.flags

    def _get_name(self):
        name_len = self.msg.name_len
        # Make sure we remove the padding bytes (although they *should* be
        # '\0', and so "reasonably safe")
        return self.msg.name[:name_len]

    def _get_data(self):
        return self.msg.data

    id          = property(_get_id)
    in_reply_to = property(_get_in_reply_to)
    to          = property(_get_to)
    from_       = property(_get_from)
    flags       = property(_get_flags)
    name        = property(_get_name)
    data        = property(_get_data)

    def extract(self):
        """Return our parts as a tuple.

        The values are returned in something approximating the order
        within the message itself:

            (id,in_reply_to,to,from_,flags,name,data_tuple)

        This is not the same order as the keyword arguments to Message().
        """
        return (self.id, self.in_reply_to, self.to, self.from_,
                self.flags, self.name, self.data)

    def to_string(self):
        """Return our data as a string
        """
        return _struct_to_string(self.msg)

    def cast(self):
        """Return (a copy of) ourselves as an appropriate subclass of Message

        Reading from a KSock returns a Message, whatever the actual message
        type. Normally, this is OK, but sometimes it would be nice to have
        an actual message of the correct class.
        """
        # If it has in_reply_to set...
        if self.in_reply_to:
            # Status messages have a specific sort of name
            if self.msg.name.startswith('$.KBUS.'):
                return Status(self)
            else:
                return Reply(self)

        # If it has the WANT_A_REPLY flag set, then it's a Request
        if self.msg.flags & Message.WANT_A_REPLY:
            return Request(self)

        # Otherwise, it's basically an Announcement (at least, that's a good bet)
        return Announcement(self)

class Announcement(Message):
    """A "plain" message, needing no reply

    This is intended to be a convenient way of constructing a message that
    is just aimed at any listeners.

    It's also a terminological convenience - all of the "message" things are
    clearly messages, so we need a special name for "plain" messages...
    There's an argument for just factory functions to create these things,
    but a class feels a little cleaner to me.

    An Announcement can be created in a variety of ways. Perhaps most obviously:

        >>> ann1 = Announcement('$.Fred',data=(0x1234,))
        >>> ann1
        Announcement('$.Fred', data=(0x1234,), to=0L, from_=0L, flags=0x00000000, id=None)

    Note that if the 'data' is specified as a string, there must be a multiple
    of 4 characters -- i.e., it must "fill out" an array of unsigned int-32
    words.

    Since Announcement is a "plain" Message, we expect to be able to use the
    normal ways of instantiating a Message for an Announcement.

    So, an Announcement can be constructed from another message directly:

        >>> ann2 = Announcement(ann1)
        >>> ann2 == ann1
        True

        >>> msg = Announcement(ann1)
        >>> ann2a = Announcement(msg)
        >>> ann2 == ann2a
        True

    Since it's an Announcement, there's no 'in_reply_to' argument

        >>> fail = Announcement('$.Fred',in_reply_to=None)
        Traceback (most recent call last):
        ...
        TypeError: __init__() got an unexpected keyword argument 'in_reply_to'

    and the 'in_reply_to' value in Message objects is ignored:

        >>> msg = Message('$.Fred',data=(0x1234,),in_reply_to=MessageId(1,2))
        >>> ann = Announcement(msg)
        >>> ann
        Announcement('$.Fred', data=(0x1234,), to=0L, from_=0L, flags=0x00000000, id=None)
        >>> print ann.in_reply_to
        None

    or from the '.extract()' tuple - again, 'reply_to' will be ignored:

        >>> ann3 = Announcement(ann1.extract())
        >>> ann3 == ann1
        True

    or from an equivalent list (and as above for 'reply_to'):

        >>> ann3 = Announcement(list(ann1.extract()))
        >>> ann3 == ann1
        True

    Or one can the same thing represented as a string:

        >>> ann_as_string = ann1.to_string()
        >>> ann4 = Announcement(ann_as_string)
        >>> ann4 == ann1
        True

    For convenience, the parts of an Announcement may be retrieved as properties:

        >>> print ann1.id
        None
        >>> ann1.name
        '$.Fred'
        >>> ann1.to
        0L
        >>> ann1.from_
        0L
        >>> print ann1.in_reply_to # always expected to be None
        None
        >>> ann1.flags
        0L
        >>> ann1.data[0]
        4660L

    Note that:

    1. An Announcement message is such because it is not a message of another
       type. There is nothing else special about it.
    """

    # I would quite like to do::
    #
    #   def __init__(self, arg, **kwargs):
    #
    # and then::
    #
    #   super(Announcement,self).__init__(arg, **kwargs)
    #
    # but then I wouldn't be able to do::
    #
    #   r = Announcement('$.Fred','data')
    #
    # which I *can* do (and want to be able to do) with Message

    def __init__(self, arg, data=None, to=None, from_=None, flags=None, id=None):
        """Arguments are the same as for Message itself, absent 'in_reply_to'.
        """
        # Just do what the caller asked for directly
        super(Announcement,self).__init__(arg, data=data, to=to,
                                          from_=from_, flags=flags, id=id)
        # And, in case 'in_reply_to' got set by that
        self.msg.in_reply_to = MessageId(0,0)

    def set_want_reply(self,value=True):
        """Announcements are not Requests.
        """
        raise TypeError("Announcements are not Requests")

    def __repr__(self):
        (id,in_reply_to,to,from_,flags,name,data) = self.extract()
        args = [repr(name),
                'data=%s'%_int_tuple_as_str(data),
                'to='+repr(to),
                'from_='+repr(from_),
                'flags=0x%08x'%flags,
                'id='+repr(id)]
        return 'Announcement(%s)'%(', '.join(args))

class Request(Message):
    """A message that wants a reply.

    This is intended to be a convenient way of constructing a message that
    wants a reply.

    It doesn't take an 'in_reply_to' initialisation argument:

        >>> fail = Request('$.Fred',in_reply_to=None)
        Traceback (most recent call last):
        ...
        TypeError: __init__() got an unexpected keyword argument 'in_reply_to'

    And it automatically sets the 'wants a reply' flag, but otherwise it
    behaves just like a Message.

    For instance, consider:

        >>> msg = Message('$.Fred',data=(0x1234,),flags=Message.WANT_A_REPLY)
        >>> msg
        Message('$.Fred', data=(0x1234,), to=0L, from_=0L, in_reply_to=None, flags=0x00000001, id=None)
        >>> req = Request('$.Fred',data=(0x1234,))
        >>> req
        Request('$.Fred', data=(0x1234,), to=0L, from_=0L, flags=0x00000001, id=None)
        >>> req == msg
        True

    Note that:

    1. A request message is a request just because it has the
       Message.WANT_A_REPLY flag set. There is nothing else special about it.
    """

    # I would quite like to do::
    #
    #   def __init__(self, arg, **kwargs):
    #
    # and then::
    #
    #   super(Request,self).__init__(arg, **kwargs)
    #
    # but then I wouldn't be able to do::
    #
    #   r = Request('$.Fred','data')
    #
    # which I *can* do (and want to be able to do) with Message

    def __init__(self, arg, data=None, to=None, from_=None, flags=None, id=None):
        """Arguments are exactly the same as for Message itself.
        """
        # First, just do what the caller asked for directly
        # but with 'in_reply_to' as 0
        super(Request,self).__init__(arg,data,to,from_,0,flags,id)
        # But then make sure that the "wants a reply" flag is set
        super(Request,self).set_want_reply(True)

    def __repr__(self):
        (id,in_reply_to,to,from_,flags,name,data) = self.extract()
        args = [repr(name),
                'data=%s'%_int_tuple_as_str(data),
                'to='+repr(to),
                'from_='+repr(from_),
                'flags=0x%08x'%flags,
                'id='+repr(id)]
        return 'Request(%s)'%(', '.join(args))

    def set_want_reply(self):
        raise TypeError('Request always has "want a reply" set')

class Reply(Message):
    """A reply message.

        (Note that the constructor for this class does *not* flip fields (such
        as 'id' and 'in_reply_to', or 'from_' and 'to') when building the Reply
        - if you want that behaviour (and you probably do), use the "reply_to"
        function.)

    Thus Reply can be used as, for instance:

        >>> direct = Reply('$.Fred', to=27, in_reply_to=MessageId(0,132))
        >>> direct
        Reply('$.Fred', data=(), to=27L, from_=0L, in_reply_to=MessageId(0,132), flags=0x00000000, id=None)
        >>> reply = Reply(direct)
        >>> direct == reply
        True

    Since a Reply is a Message with its 'in_reply_to' set, this *must* be provided:

        >>> msg = Message('$.Fred',data=(0x1234,),from_=27,to=99,id=MessageId(0,132),flags=Message.WANT_A_REPLY)
        >>> msg
        Message('$.Fred', data=(0x1234,), to=99L, from_=27L, in_reply_to=None, flags=0x00000001, id=MessageId(0,132))
        >>> reply = Reply(msg)
        Traceback (most recent call last):
        ...
        ValueError: A Reply must specify in_reply_to

        >>> reply = Reply(msg,in_reply_to=MessageId(0,5))
        >>> reply
        Reply('$.Fred', data=(0x1234,), to=99L, from_=27L, in_reply_to=MessageId(0,5), flags=0x00000001, id=MessageId(0,132))

    It's also possible to construct a Reply in most of the other ways a Message
    can be constructed. For instance:

        >>> rep2 = Reply(direct.to_string())
        >>> rep2 == direct
        True
        >>> rep4 = Reply(direct.extract())
        >>> rep4 == direct
        True
    """

    def __init__(self, arg, data=None, to=None, from_=None, in_reply_to=None, flags=None, id=None):
        """Just do what the user asked, but they must give 'in_reply_to'.
        """
        
        super(Reply,self).__init__(arg, data=data, to=to, from_=from_,
                                   in_reply_to=in_reply_to, flags=flags, id=id)
        if self.in_reply_to is None:
            raise ValueError("A Reply must specify in_reply_to")

    def __repr__(self):
        (id,in_reply_to,to,from_,flags,name,data) = self.extract()
        args = [repr(name),
                'data=%s'%_int_tuple_as_str(data),
                'to='+repr(to),
                'from_='+repr(from_),
                'in_reply_to='+repr(in_reply_to),
                'flags=0x%08x'%flags,
                'id='+repr(id)]
        return 'Reply(%s)'%(', '.join(args))

class Status(Message):
    """A status message, from KBUS.

    This is provided as a sugar-coating around the messages KBUS sends us. As
    such, it is not expected that a normal user would want to construct one,
    and the initialisation mechanisms are correspondingly more restrictive.

    For instance:

        >>> msg = Message('$.KBUS.Dummy',from_=27,to=99,in_reply_to=MessageId(0,132))
        >>> msg
        Message('$.KBUS.Dummy', data=(), to=99L, from_=27L, in_reply_to=MessageId(0,132), flags=0x00000000, id=None)
        >>> status = Status(msg.to_string())
        >>> status
        Status('$.KBUS.Dummy', data=(), to=99L, from_=27L, in_reply_to=MessageId(0,132), flags=0x00000000, id=None)

    Note that:

    1. A status message is such because it is a (sort of) Reply, with the
       message name starting with '$.KBUS.'.
    """

    def __init__(self, original):
        # Actually, this is slightly more forgiving than the docstring
        # suggests, but conversely I'm not going to hold the user's hand
        # if they do something that's not supported...
        super(Status,self).__init__(original)

    def __repr__(self):
        (id,in_reply_to,to,from_,flags,name,data) = self.extract()
        args = [repr(name),
                'data=%s'%_int_tuple_as_str(data),
                'to='+repr(to),
                'from_='+repr(from_),
                'in_reply_to='+repr(in_reply_to),
                'flags=0x%08x'%flags,
                'id='+repr(id)]
        return 'Status(%s)'%(', '.join(args))

def reply_to(original, data=None, flags=0):
    """Return a Reply to the given Message.

    This is intended to be the normal way of constructing a reply message.

    For instance:

        >>> msg = Message('$.Fred',data=(0x1234,),from_=27,to=99,id=MessageId(0,132),flags=Message.WANT_A_REPLY)
        >>> msg
        Message('$.Fred', data=(0x1234,), to=99L, from_=27L, in_reply_to=None, flags=0x00000001, id=MessageId(0,132))
        >>> reply = reply_to(msg)
        >>> reply
        Reply('$.Fred', data=(), to=27L, from_=0L, in_reply_to=MessageId(0,132), flags=0x00000000, id=None)

    Note that:

    1. A reply message is a reply because it has the 'in_reply_to' field set.
       This indicates the message id of the original message, the one we're
       replying to.
    2. As normal, the Reply's own message id is unset - KBUS will set this, as
       for any message.
    3. We give a specific 'to' value, the id of the KSock that sent the
       original message, and thus the 'from' value in the original message.
    4. We keep the same message name, but don't copy the original message's
       data. If we want to send data in a reply message, it will be our own
       data.

    It's also possible to construct a Reply from an equivalent string, although
    these always act as if they were::

                m = Message(arg)
                r = reply_to(m)

    which means that the automatic swapping of from/to, etc., will happen.
    For instance:

        >>> rep2 = reply_to(msg.to_string())
        >>> rep2 == reply
        True

    The other arguments available are 'flags' (allowing the setting of flags
    such as Message.ALL_OR_WAIT, for instance), and 'data', allowing reply data
    to be added:

        >>> rep4 = reply_to(msg,flags=Message.ALL_OR_WAIT,data=(0x1234,))
        >>> rep4
        Reply('$.Fred', data=(0x1234,), to=27L, from_=0L, in_reply_to=MessageId(0,132), flags=0x00000100, id=None)
    """

    if not isinstance(original,Message):
        # The lazy way of handling this case
        original = Message(original)
        # and then fall through into...

    (id,in_reply_to,to,from_,original_flags,name,data_tuple) = original.extract()
    # We reply to the original sender (to), indicating which message we're
    # responding to (in_reply_to).
    #
    # The fact that in_reply_to is set means that we *are* a reply.
    #
    # We don't need to set any flags. We definitely *don't* want to copy
    # any flags from the original message.
    return Reply(name, data=data, in_reply_to=id, to=from_, flags=flags)

class KbusBindStruct(ctypes.Structure):
    """The datastucture we need to describe an IOC_BIND argument
    """
    _fields_ = [('is_replier', ctypes.c_uint32),
                ('len',        ctypes.c_uint32),
                ('name',       ctypes.c_char_p)]

class KbusListenerStruct(ctypes.Structure):
    """The datastucture we need to describe an IOC_REPLIER argument
    """
    _fields_ = [('return_id', ctypes.c_uint32),
                ('len',       ctypes.c_uint32),
                ('name',      ctypes.c_char_p)]

class KbusSendResultStruct(ctypes.Structure):
    """The datastucture we need to describe an IOC_SEND argument/return
    """
    _fields_ = [('retval',  ctypes.c_int32),
                ('msg_id',  MessageId)]

class KbusMessageHeaderStruct(ctypes.Structure):
    """The datastructure for a Message header.
    """
    _fields_ = [('start_guard', ctypes.c_uint32),
                ('id',          MessageId),
                ('in_reply_to', MessageId),
                ('to',          ctypes.c_uint32),
                ('from_',       ctypes.c_uint32), # named consistently with elsewhere
                ('flags',       ctypes.c_uint32),
                ('name_len',    ctypes.c_uint32),
                ('data_len',    ctypes.c_uint32)]

    def __repr__(self):
        """For debugging, not construction of an instance of ourselves.
        """
        return "<%08x] %s %s %u %u %08x %u %u"%(
                self.start_guard,
                self.id._short_str(),
                self.in_reply_to._short_str(),
                self.to,
                self.from_,
                self.flags,
                self.name_len,
                self.data_len)

    def __eq__(self,other):
        if not isinstance(other,KbusMessageHeaderStruct):
            return False
        else:
            return (self.id == other.id and
                    self.in_reply_to == other.in_reply_to and
                    self.to == other.to and
                    self.from_ == other.from_ and
                    self.flags == other.flags and
                    self.name_len == other.name_len and
                    self.data_len == other.data_len)

    def __ne__(self,other):
        if not isinstance(other,KbusMessageHeaderStruct):
            return True
        else:
            return (self.id != other.id or
                    self.in_reply_to != other.in_reply_to or
                    self.to != other.to or
                    self.from_ != other.from_ or
                    self.flags != other.flags or
                    self.name_len != other.name_len or
                    self.data_len != other.data_len)

def _struct_to_string(struct):
    return ctypes.string_at(ctypes.addressof(struct), ctypes.sizeof(struct))

def _struct_from_string(struct_class, data):
    thing = struct_class()
    ctypes.memmove(ctypes.addressof(thing), data, ctypes.sizeof(thing))
    return thing

class KbusEntireMessageStructBaseclass(ctypes.Structure):
    """The baseclass for our "entire message structure".

    Defined separately just to reduce the amount of code executed in the
    functions that *build* the classes.

    It is required that the fields defined be 'header', 'name', 'data'
    and 'end_guard' -- but since I'm assuming this will only be (directly)
    used internally to kbus.py, I'm happy with that.

        (Specifically, see the ``_specific_entire_message_struct`` function)
    """

    def __repr__(self):
        """For debugging, not construction of an instance of ourselves.
        """
        return "%s '%s' %s [%08x>"%(
                self.header,
                self.name[:self.name_len],
                _int_tuple_as_str(self.data),
                self.end_guard)

    def __eq__(self,other):
        if not isinstance(other,KbusEntireMessageStructBaseclass):
            return False
        else:
            return (self.header == other.header and
                    self.name == other.name and
                    self._data_eq(other))

    def __ne__(self,other):
        if not isinstance(other,KbusEntireMessageStructBaseclass):
            return True
        else:
            return (self.id != other.id or
                    self.name != other.name or
                    self._data_ne(other))

    def _data_eq(self,other):
        if len(self.data) != len(other.data):
            return False
        for (a,b) in itertools.izip(self.data, other.data):
            if a != b:
                return False
        return True

    def _data_ne(self,other):
        if len(self.data) != len(other.data):
            return True
        for (a,b) in itertools.izip(self.data, other.data):
            if a != b:
                return True
        return False

def _int_tuple_as_str(data):
    """Return a representation of a tuple of integers, as a string.
    """
    words = []
    for w in data:
        words.append('0x%x'%w)

    if len(words) == 0:
        return '()'
    elif len(words) == 1:
        return '(%s,)'%words[0]
    else:
        return '(%s)'%(', '.join(words))

# Is this premature optimisation?
# I don't think Python would cache the different classes for me,
# and it seems wasteful to create a new class for *every* message,
# given there will be a lot of messages that are very similar...
_specific_entire_message_struct_dict = {}

def _specific_entire_message_struct(padded_name_len, data_len):
    """Return a specific subclass of KbusMessageHeaderStruct
    """
    key = (padded_name_len,data_len)
    if key in _specific_entire_message_struct_dict:
        return _specific_entire_message_struct_dict[key]
    else:
        class localKbusEntireMessageStruct(KbusEntireMessageStructBaseclass):
            _fields_ = [('header',     KbusMessageHeaderStruct),
                        ('name',       ctypes.c_char   * padded_name_len),
                        ('data',       ctypes.c_uint32 * data_len),
                        ('end_guard',  ctypes.c_uint32)]
            # Allow the user to type '.to' instead of '.header.to'
            _anonymous_ = ('header',)
        _specific_entire_message_struct_dict[key] = localKbusEntireMessageStruct
        return localKbusEntireMessageStruct

def entire_message_from_parts(id, in_reply_to, to, from_, flags, name, data):
    """Return a new KbusEntireMessageStruct of the correct shape.

    - 'id' and 'in_reply_to' are (network_id,serial_num) tuples
    - 'to', 'in_reply_to' and 'from_' are 0 or a KSock id
    - 'name' is a string
    - 'data' is a tuple of integers
    """
    name_len = len(name)
    data_len = len(data)

    # Remember that the message name itself needs padding out to 4-bytes
    # ...this is about the nastiest way possible of doing it...
    while len(name)%4:
        name += '\0'

    padded_name_len = len(name)

    header = KbusMessageHeaderStruct(Message.START_GUARD,
                                     id, in_reply_to,
                                     to, from_, flags, name_len, data_len)

    # We rather rely on 'data' "disappearing" (being of zero length)
    # if 'data_len' is zero, and it appears that that just works.

    local_class = _specific_entire_message_struct(padded_name_len, data_len)

    return local_class(header, name, data, Message.END_GUARD)

def entire_message_from_string(data):
    """Return a KbusEntireMessageStruct of a size that satisfies.

    'data' is a string-like object (as, for instance, returned by 'read')
    """
    h = _struct_from_string(KbusMessageHeaderStruct, data)

    padded_name_len = 4*((h.name_len + 3) / 4)

    local_class = _specific_entire_message_struct(padded_name_len, h.data_len)

    return _struct_from_string(local_class, data)

class KSock(object):
    """A wrapper around a KBUS device, for purposes of message sending.

    'which' is which KBUS device to open -- so if 'which' is 3, we open
    /dev/kbus3.

    'mode' should be 'r' or 'rw' -- i.e., whether to open the device for read or
    write (opening for write also allows reading, of course).

    I'm not really very keen on the name KSock, but it's better than the
    original "File", which I think was actively misleading.
    """

    IOC_MAGIC = 'k'
    IOC_RESET    = _IO(IOC_MAGIC,   1)
    IOC_BIND     = _IOW(IOC_MAGIC,  2, ctypes.sizeof(ctypes.c_char_p))
    IOC_UNBIND   = _IOW(IOC_MAGIC,  3, ctypes.sizeof(ctypes.c_char_p))
    IOC_KSOCKID  = _IOR(IOC_MAGIC,  4, ctypes.sizeof(ctypes.c_char_p))
    IOC_REPLIER  = _IOWR(IOC_MAGIC, 5, ctypes.sizeof(ctypes.c_char_p))
    IOC_NEXTMSG  = _IOR(IOC_MAGIC,  6, ctypes.sizeof(ctypes.c_char_p))
    IOC_LENLEFT  = _IOR(IOC_MAGIC,  7, ctypes.sizeof(ctypes.c_char_p))
    IOC_SEND     = _IOR(IOC_MAGIC,  8, ctypes.sizeof(ctypes.c_char_p))
    IOC_DISCARD  = _IO(IOC_MAGIC,   9)
    IOC_LASTSENT = _IOR(IOC_MAGIC, 10, ctypes.sizeof(ctypes.c_char_p))
    IOC_MAXMSGS  = _IOWR(IOC_MAGIC,11, ctypes.sizeof(ctypes.c_char_p))
    IOC_NUMMSGS  = _IOR(IOC_MAGIC, 12, ctypes.sizeof(ctypes.c_char_p))

    def __init__(self,which=0,mode='r'):
        if mode not in ('r','rw'):
            raise ValueError("KSock mode should be 'r' or 'rw', not '%s'"%mode)
        self.which = which
        self.name = '/dev/kbus%d'%which
        if mode == 'r':
            self.mode = 'read'
        else:
            mode = 'w+'
            self.mode = 'read/write'
        # Although Unix doesn't mind whether a file is opened with a 'b'
        # for binary, it is possible that some version of Python may
        self.fd = open(self.name,mode+'b')

    def __repr__(self):
        if self.fd:
            return '<KSock %s open for %s>'%(self.name,self.mode)
        else:
            return '<KSock %s closed>'%(self.name)

    def close(self):
        ret = self.fd.close()
        self.fd = None
        self.mode = None
        return ret

    def bind(self,name,replier=False):
        """Bind the given name to the file descriptor.

        If 'replier', then we are binding as the only fd that can reply to this
        message name.
        """
        arg = KbusBindStruct(replier,len(name),name)
        return fcntl.ioctl(self.fd, KSock.IOC_BIND, arg)

    def unbind(self,name,replier=False):
        """Unbind the given name from the file descriptor.

        The arguments need to match the binding that we want to unbind.
        """
        arg = KbusBindStruct(replier,len(name),name)
        return fcntl.ioctl(self.fd, KSock.IOC_UNBIND, arg)

    def ksock_id(self):
        """Return the internal 'KSock id' for this file descriptor.
        """
        # Instead of using a ctypes.Structure, we can retrieve homogenious
        # arrays of data using, well, arrays. This one is a bit minimalist.
        id = array.array('L',[0])
        fcntl.ioctl(self.fd, KSock.IOC_KSOCKID, id, True)
        return id[0]

    def next_msg(self):
        """Say we want to start reading the next message.

        Returns the length of said message, or 0 if there is no next message.
        """
        id = array.array('L',[0])
        fcntl.ioctl(self.fd, KSock.IOC_NEXTMSG, id, True)
        return id[0]

    def len_left(self):
        """Return how many bytes of the current message are still to be read.

        Returns 0 if there is no current message (i.e., 'next_msg()' has not
        been called), or if there are no bytes left.
        """
        id = array.array('L',[0])
        fcntl.ioctl(self.fd, KSock.IOC_LENLEFT, id, True)
        return id[0]

    def send(self):
        """Send the last written message.

        Indicates that we have finished writing a message, and it should
        be sent.

        Returns the message id of the send message.

        Raises IOError with errno ENOMSG if there was no message to send.
        """
        arg = array.array('L',[0,0])
        fcntl.ioctl(self.fd, KSock.IOC_SEND, arg);
        return MessageId(arg[0],arg[1])

    def discard(self):
        """Discard the message being written.

        Indicates that we have should throw away the message we've been
        writing. Has no effect if there is no current message being
        written (for instance, because 'send' has already been called).
        be sent.
        """
        return fcntl.ioctl(self.fd, KSock.IOC_DISCARD, 0);

    def last_msg_id(self):
        """Return the id of the last message written on this file descriptor.

        Returns 0 before any messages have been sent.
        """
        id = array.array('L',[0,0])
        fcntl.ioctl(self.fd, KSock.IOC_LASTSENT, id, True)
        return MessageId(id[0],id[1])

    def find_replier(self,name):
        """Find the id of the replier (if any) for this message.

        Returns None if there was no replier, otherwise the replier's id.
        """
        arg = KbusListenerStruct(0,len(name),name)
        retval = fcntl.ioctl(self.fd, KSock.IOC_REPLIER, arg);
        if retval:
            return arg.return_id
        else:
            return None

    def max_messages(self):
        """Return the number of messages that can be queued on this KSock.
        """
        id = array.array('L',[0])
        fcntl.ioctl(self.fd, KSock.IOC_MAXMSGS, id, True)
        return id[0]

    def set_max_messages(self,count):
        """Set the number of messages that can be queued on this KSock.

        A 'count' of 0 does not actually change the value - this may thus be
        used to query the KSock for the current value of the maximum.
        However, the "more Pythonic" 'max_messages()' method is provided for
        use when such a query is wanted, which is just syntactic sugar around
        such a call.

        Returns the number of message that are allowed to be queued on this
        KSock.
        """
        id = array.array('L',[count])
        fcntl.ioctl(self.fd, KSock.IOC_MAXMSGS, id, True)
        return id[0]

    def num_messages(self):
        """Return the number of messages that are queued on this KSock.
        """
        id = array.array('L',[0])
        fcntl.ioctl(self.fd, KSock.IOC_NUMMSGS, id, True)
        return id[0]

    def write_msg(self,message):
        """Write a Message. Doesn't send it.
        """
        # Message data is held in an array.array, and arrays know
        # how to write themselves out
        self.fd.write(message.msg)
        # But we are responsible for flushing
        self.fd.flush()

    def send_msg(self,message):
        """Write a Message, and then send it.

        Entirely equivalent to calling 'write_msg' and then 'send',
        and returns the MessageId of the sent message, as 'send' does.
        """
        self.write_msg(message)
        return self.send()

    def write_data(self,data):
        """Write out (and flush) some data.

        Does not send it, does not imply that it is all of a message
        (although clearly it should form *some* of a message).
        """
        ret = self.fd.write(data)
        self.fd.flush()
        return ret

    def read_msg(self,length):
        """Read a Message of length 'length' bytes.

        It is assumed that 'length' was returned by a previous call
        of 'next_msg'. It must be large enough to cause the entire
        message to be read.

        After the data has been read, it is passed to Message to
        construct a message instance, which is returned.

        Returns None if there was nothing to be read.
        """
        data = self.fd.read(length)
        if data:
            return Message(data)
        else:
            return None

    def read_next_msg(self):
        """Read the next Message.

        Equivalent to a call of 'next_msg', followed by reading the
        appropriate number of bytes and passing that to Message to
        construct a message instance, which is returned.

        Returns None if there was nothing to be read.
        """
        data = self.fd.read(self.next_msg())
        if data:
            return Message(data)
        else:
            return None

    def read_data(self,count):
        """Read the next 'count' bytes, and return them.

        Returns '' (the empty string) if there was nothing to be read,
        which is consistent with now Python file reads normally behave
        at end-of-file.
        """
        return self.fd.read(count)

    # It's modern times, so we really should implement "with"
    # (it's so convenient)

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

    # And what is a file-like object without iterator support?
    # Note that our iteration will stop when there is no next message
    # to read -- so trying to iterate again later on may work again...

    def __iter__(self):
        return self

    def next(self):
        msg = self.read_next_msg()
        if msg == None:
            raise StopIteration
        else:
            return msg

    def fileno(self):
        """Return the integer file descriptor from our internal fd.

        This allows a KSock instance to be used in a call of select.select()
        - so, for instance, on should be able to do::

            (r,w,x) = select.select([ksock1,ksock2,ksock3],None,None)

        instead of the (less friendly, but also valid)::

            (r,w,x) = select.select([ksock1.fd,ksock2.fd,ksock3.fd],None,None)
        """
        return self.fd.fileno()

def read_bindings(names):
    """Read the bindings from /proc/kbus/bindings, and return a list

    /proc/kbus/bindings gives us data like::

            0: 10 R $.Fred
            0: 11 L $.Fred.Bob
            0: 12 R $.William

    'names' is a dictionary of file descriptor binding id to string (name)
    - for instance:

        { 10:'f1', 11:'f2' }

    If there is no entry in the 'names' dictionary for a given id, then the
    id will be used (as an integer).

    Thus with the above we would return a list of the form::

        [ ('f1',True,'$.Fred'), ('f2',False,'$.Fred.Bob'),
          (12,True,'$.William' ]
    """
    f = open('/proc/kbus/bindings')
    l = f.readlines()
    f.close()
    bindings = []
    for line in l:
        # 'dev' is the device index (default is 0, may be 0..9 depending on how
        # many /dev/kbus<N> devices there are).
        # For the moment, we're going to ignore it.
        dev,id,rep,name = line.split()
        id = int(id)
        if id in names:
            id = names[int(id)]
        if rep == 'R':          # Replier
            rep = True
        elif rep == 'L':        # (just a) Listener
            rep = False
        else:
            raise ValueError,"Got replier '%c' when expecting 'R' or 'L'"%rep
        bindings.append((id,rep,name))
    return bindings

# vim: set tabstop=8 shiftwidth=4 expandtab:
