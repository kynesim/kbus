"""The definition of a KBUS Message (and its subclasses).

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
#   Tibs <tibs@tonyibbs.co.uk>
#
# ***** END LICENSE BLOCK *****

import ctypes
import array
import string
import struct


def _BIT(nr):
    return 1 << nr

def _set_bit(value, which):
    """Return 'value' with the bit 'which' set.

    'which' should be something like _BIT(3).
    """
    return value | which

def _clear_bit(value, which):
    """Return 'value' with the bit 'which' cleared.

    'which' should be something like _BIT(3).
    """
    if value & which:
        mask = ~which
        value = value & mask
    return value

class MessageId(ctypes.Structure):
    """A wrapper around a message id.

        >>> a = MessageId(1, 2)
        >>> a
        MessageId(1, 2)
        >>> a < MessageId(2, 2) and a < MessageId(1, 3)
        True
        >>> a == MessageId(1, 2)
        True
        >>> a > MessageId(0, 2) and a > MessageId(1, 1)
        True

    We support addition in a limited manner:

        >>> a + 3
        MessageId(1, 5)

    simply to make it convenient to generate unique message ids. This returns
    a new MessageId - it doesn't amend the existing one.
    """
    _fields_ = [('network_id', ctypes.c_uint32),
                ('serial_num', ctypes.c_uint32)]

    def __repr__(self):
        return 'MessageId(%u, %u)'%(self.network_id, self.serial_num)

    def _short_str(self):
        """For use in message structure reporting
        """
        return '%u:%u'%(self.network_id, self.serial_num)

    def __str__(self):
        return '[%u:%u]'%(self.network_id, self.serial_num)

    def __eq__(self, other):
        if not isinstance(other, MessageId):
            return NotImplemented
        return (self.network_id == other.network_id and
                self.serial_num == other.serial_num)

    def __lt__(self, other):
        if not isinstance(other, MessageId):
            return NotImplemented
        if self.network_id < other.network_id:
            return True
        if self.network_id > other.network_id:
            return False
        return self.serial_num < other.serial_num

    def __add__(self, other):
        if not isinstance(other, int):
            return NotImplemented
        else:
            return MessageId(self.network_id, self.serial_num+other)

class OrigFrom(ctypes.Structure):
    """A wrapper to the underlying C `struct kbus_orig_from` type.
    This is the type of :data:`Message.orig_from` and :data:`Message.final_to`.

        >>> a = OrigFrom(1, 2)
        >>> a
        OrigFrom(1, 2)
        >>> a < OrigFrom(2, 2) and a < OrigFrom(1, 3)
        True
        >>> a == OrigFrom(1, 2)
        True
        >>> a > OrigFrom(0, 2) and a > OrigFrom(1, 1)
        True
    """
    _fields_ = [('network_id', ctypes.c_uint32),
                ('local_id',   ctypes.c_uint32)]

    def __repr__(self):
        return 'OrigFrom(%u, %u)'%(self.network_id, self.local_id)

    def _short_str(self):
        """For use in message structure reporting
        """
        return '%u,%u'%(self.network_id, self.local_id)

    def __str__(self):
        return '(%u,%u)'%(self.network_id, self.local_id)

    def __eq__(self, other):
        if not isinstance(other, OrigFrom):
            return NotImplemented

        return (self.network_id == other.network_id and
                self.local_id == other.local_id)

    def __lt__(self, other):
        if not isinstance(other, OrigFrom):
            return NotImplemented

        if self.network_id < other.network_id:
            return True
        if self.network_id > other.network_id:
            return False
        return self.local_id < other.local_id

def _same_message_struct(this, that):
    """Returns true if the two message structures are the same.

    Copes with both "plain" and "entire" messages (i.e., those
    which are a _MessageHeaderStruct with pointers to name and data,
    and also those which are an 'EntireMessageStruct', with the
    name and (any) data concatenated after the header).
    """
    if not isinstance(this, _MessageHeaderStruct) and \
       not isinstance(this, _EntireMessageStructBaseclass):
        return False

    if not isinstance(that, _MessageHeaderStruct) and \
       not isinstance(that, _EntireMessageStructBaseclass):
        return False

    if (this.id != that.id or
        this.in_reply_to != that.in_reply_to or
        this.to != that.to or
        this.from_ != that.from_ or
        this.orig_from != that.orig_from or
        this.final_to  != that.final_to or
        this.flags != that.flags or
        this.name_len != that.name_len or
        this.data_len != that.data_len or
        this.name != that.name):
        return False

    if this.data_len:
        this_data = c_data_as_bytes(this.data, this.data_len)
        that_data = c_data_as_bytes(that.data, that.data_len)
        return this_data == that_data
    return True

def _equivalent_message_struct(this, that):
    """Returns true if the two messages are mostly the same.

    For purposes of this comparison, we ignore:

    * 'id',
    * 'flags',
    * 'in_reply_to',
    * 'from' and
    * 'orig_from' and 'final_to'

    and, of course 'extra'.

    Copes with both "plain" and "entire" messages (i.e., those which are a
    _MessageHeaderStruct with pointers to name and data, and also those which
    are an 'EntireMessageStruct', with the name and (any) data concatenated
    after the header).
    """
    if not isinstance(this, _MessageHeaderStruct) and \
       not isinstance(this, _EntireMessageStructBaseclass):
        return False

    if not isinstance(that, _MessageHeaderStruct) and \
       not isinstance(that, _EntireMessageStructBaseclass):
        return False

    if (this.to != that.to or
        this.name_len != that.name_len or
        this.data_len != that.data_len or
        this.name != that.name):
        return False

    if this.data_len:
        this_data = c_data_as_bytes(this.data, this.data_len)
        that_data = c_data_as_bytes(that.data, that.data_len)
        return this_data == that_data
    return True

def c_data_as_bytes(data, data_len):
    """Return the message data as a string.
    """
    return bytes(data[:data_len])

def hexdata(data, encoding="utf-8", errors="strict"):
    r"""Return a representation of a 'string' in printable form.

    If the string is a Python string rather than a bytes object, it will be
    encoded using `encoding` and `errors` exactly as :meth:`str.encode()`.

    Doesn't use whitespace or anything not in letters, digits or punctuation.
    Thus, the resultant string should be entirely equivalent in "meaning" to
    the input.

    For instance:

        >>> hexdata('1234')
        '1234'
        >>> hexdata('')
        ''
        >>> hexdata(' ')
        '\\x20'
        >>> hexdata('\x27')
        "'"
        >>> hexdata('\x03')
        '\\x03'
    """
    pretty = string.ascii_letters + string.digits + string.punctuation
    words = []
    if isinstance(data, str):
        data = data.encode(encoding=encoding, errors=errors)
    for ch in data:
        if chr(ch) in pretty:
            words.append(chr(ch))
        else:
            words.append('\\x%02x'%ch)
    return ''.join(words)

def hexify(data, encoding="utf-8", errors="strict"):
    r"""Return a representation of a 'string' as hex values.

    If the string is a Python string rather than a bytes object, it will be
    encoded using `encoding` and `errors` exactly as :meth:`str.encode()`.

    For instance:

        >>> hexify('1234')
        '31 32 33 34'
        >>> hexify('')
        ''
        >>> hexify('\x27')
        '27'
    """
    words = []
    if isinstance(data, str):
        data = data.encode(encoding=encoding, errors=errors)
    for ch in data:
        words.append('%02x'%ch)
    return ' '.join(words)

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

class _MessageHeaderStruct(ctypes.Structure):
    """The datastructure for a Message header.

    A "plain" message is represented as a Message header with pointers
    to its name and (any) data.
    """
    _fields_ = [('start_guard', ctypes.c_uint32),
                ('id',          MessageId),
                ('in_reply_to', MessageId),
                ('to',          ctypes.c_uint32),
                ('from_',       ctypes.c_uint32), # named consistently with elsewhere
                ('orig_from',   OrigFrom),
                ('final_to',    OrigFrom),
                ('extra',       ctypes.c_uint32),
                ('flags',       ctypes.c_uint32),
                ('name_len',    ctypes.c_uint32),
                ('data_len',    ctypes.c_uint32),
                ('name',        ctypes.c_char_p),
                ('data',        ctypes.POINTER(ctypes.c_uint8)),
                ('end_guard',   ctypes.c_uint32)]

    is_pointy = True

    def __repr__(self):
        """For debugging, not construction of an instance of ourselves.
        """
        if self.name == None:
            nn = 'None'
        else:
            nn = repr(hexdata(self.name))
        if self.data == None:
            dd = 'None'
        else:
            # We need to retrieve our data from the pointer - ick
            p = ctypes.cast(self.data, ctypes.POINTER(ctypes.c_uint8*self.data_len))
            s = ctypes.string_at(p, self.data_len)
            dd = repr(hexdata(s))
        return "<%08x] %s %s %u %u %s %s %08x %u %u %s %s [%08x>"%(
                self.start_guard,
                self.id._short_str(),
                self.in_reply_to._short_str(),
                self.to,
                self.from_,
                self.orig_from._short_str(),
                self.final_to._short_str(),
                self.flags,
                self.name_len,
                self.data_len,
                nn,
                dd,
                self.end_guard)

    def __eq__(self, other):
        return _same_message_struct(self, other)

    def __ne__(self, other):
        return not _same_message_struct(self, other)

    def equivalent(self, other):
        """Returns true if the two messages are mostly the same.

        For purposes of this comparison, we ignore:

        * 'id',
        * 'flags',
        * 'in_reply_to' and
        * 'from'

        Copes with both "plain" and "entire" messages (i.e., those
        which are a _MessageHeaderStruct with pointers to name and data,
        and also those which are an 'EntireMessageStruct', with the
        name and (any) data concatenated after the header).
        """
        return _equivalent_message_struct(self, other)

def _struct_to_bytes(struct):
    """Return the internal datastructure of 'struct' as  bytes.

    Note that this may be slightly longer than you expect, if the underlying
    C datastructure has padding. Remember this padding may occur at the END
    of a datatructure, too.
    """
    return ctypes.string_at(ctypes.addressof(struct), ctypes.sizeof(struct))

def _struct_from_bytes(struct_class, data, encoding="utf-8", errors="strict"):
    thing = struct_class()
    if isinstance(data, str):
        data = data.encode(encoding=encoding, errors=errors)
    ctypes.memmove(ctypes.addressof(thing), data, ctypes.sizeof(thing))
    return thing

MSG_HEADER_LEN = ctypes.sizeof(_MessageHeaderStruct)

def calc_padded_name_len(name_len):
    """Calculate the length of a message name, in bytes, after padding.

    Matches the definition in the kernel module's header file
    """
    return 4 * ((name_len + 1 + 3) // 4)

def calc_padded_data_len(data_len):
    """Calculate the length of message data, in bytes, after padding.

    Matches the definition in the kernel module's header file
    """
    return 4 * ((data_len + 3) // 4)

def calc_entire_message_len(name_len, data_len):
    """Calculate the "entire" message length, from the name and data lengths.

    All lengths are in bytes.

    Matches the definition in the kernel module's header file
    """
    return MSG_HEADER_LEN + calc_padded_name_len(name_len) + \
                            calc_padded_data_len(data_len) + 4

def message_from_parts(id, in_reply_to, to, from_, orig_from, final_to, flags, name, data, encoding="utf-8", errors="strict"):
    """Return a new Message header structure, with name and data attached.

    - 'id' and 'in_reply_to' are (network_id, serial_num) tuples
    - 'to', 'in_reply_to' and 'from_' are 0 or a Ksock id
    - 'orig_from' and 'final_to' are None or a (network_id, local_id) tuple
    - 'name' is a (UTF-8) string
    - 'data' is a bytes object, a string or None
    - 'encoding' is the encoding to use if 'data' is a string
    - 'errors' is the error handling to use for encoding if 'data' is a string
    """
    if orig_from is None:
        orig_from = (0,0)

    if final_to is None:
        final_to = (0,0)

    name = bytes(name, "utf-8")
    name_len = len(name)

    if data:
        if isinstance(data, str):
            data = data.encode(encoding=encoding, errors=errors)
        data_len = len(data)
    else:
        data_len = 0

    # C wants us to have a terminating 0 byte
    name += b'\0'
    # And we want to pad the result out to a multiple of 4 bytes
    # ...this is about the nastiest way possible of doing it...
    while len(name)%4:
        name += b'\0'

    padded_name_len = len(name)

    # We want to pad the data out in the same manner
    # (but without the terminating 0 byte)
    if data:
        while len(data)%4:
            data += b'\0'
        padded_data_len = len(data)
    else:
        padded_data_len = 0

    name_ptr = ctypes.c_char_p(name)
    if data:
        # This seems a bit clumsy and wasteful, but I can't see
        # how else to do it
        DataArray = ctypes.c_uint8 * padded_data_len
        data_ptr = DataArray( *data )
        #data_ptr = DataArray(*data)
    else:
        data_ptr = None

    return _MessageHeaderStruct(Message.START_GUARD,
                                id, in_reply_to,
                                to, from_, orig_from, final_to, 0, flags,
                                name_len, data_len,
                                name_ptr, data_ptr, Message.END_GUARD)

def _pointy_message_from_bytes(msg_data):
    """Return a "pointy" message structure from the given data.
    """
    h = _struct_from_bytes(_MessageHeaderStruct, msg_data)

    # Don't forget that the string will be terminated with a 0 byte
    padded_name_len = calc_padded_name_len(h.name_len)

    # But not so the data
    padded_data_len = calc_padded_data_len(h.data_len)

    name_offset = MSG_HEADER_LEN

    h.name = msg_data[name_offset:name_offset+h.name_len]

    data_offset = name_offset + padded_name_len

    if h.data_len == 0:
        h.data = None
    else:
        data = msg_data[data_offset:data_offset+h.data_len]

        DataArray = ctypes.c_uint8 * h.data_len
        h.data = DataArray( *[ord(x) for x in data] )

    final_end_guard = msg_data[data_offset+padded_data_len:]
    return h

class _EntireMessageStructBaseclass(ctypes.Structure):
    """The baseclass for our "entire" message structure.

    Defined separately just to reduce the amount of code executed in the
    functions that *build* the classes.

    It is required that the fields defined be 'header', 'rest_name',
    'rest_data' and 'rest_end_guard' -- but since I'm assuming this will only
    be (directly) used internally to kbus.py, I'm happy with that.

        (Specifically, see the ``_specific_entire_message_struct`` function)
    """

    def __repr__(self):
        """For debugging, not construction of an instance of ourselves.
        """
        if self.name_len:
            name_repr = repr(hexdata(self.rest_name[:self.name_len]))
        else:
            name_repr = 'None'
        if self.data_len:
            data_repr = repr(hexdata(ctypes.string_at(self.rest_data, self.data_len)))
        else:
            data_repr = None
        return "%s %s %s [%08x>"%(
                self.header,
                name_repr,
                data_repr,
                self.rest_end_guard)

    # If we didn't have the problem of trying to look for the
    # message name and data in the "rest" of the structure, we
    # could use the "anonymous" capability to make the "header"
    # names be used directly. But that would not allow us to
    # fudge "name" and "data", so...

    is_pointy = False

    @property
    def start_guard(self):
        return self.header.start_guard

    @property
    def id(self):
        return self.header.id

    @property
    def in_reply_to(self):
        return self.header.in_reply_to

    # Announcement wants to be able to overwrite in_reply_to
    @in_reply_to.setter
    def in_reply_to(self, value):
        self.header.in_reply_to = value

    @property
    def to(self):
        return self.header.to

    # Limpets want to be able to overwrite to
    @to.setter
    def to(self, value):
        self.header.to = value

    @property
    def from_(self):
        return self.header.from_

    @property
    def orig_from(self):
        return self.header.orig_from

    # Announcement and Status would quite like to be able to overwrite orig_from
    @orig_from.setter
    def orig_from(self, value):
        self.header.orig_from = value

    @property
    def final_to(self):
        return self.header.final_to

    # Announcement and Status would quite like to be able to overwrite final_to
    @final_to.setter
    def final_to(self, value):
        self.header.final_to = value

    @property
    def extra(self):
        return self.header.extra

    @property
    def flags(self):
        return self.header.flags

    # It's useful to be able to set flags
    @flags.setter
    def flags(self, value):
        self.header.flags = value

    @property
    def name_len(self):
        return self.header.name_len

    @property
    def data_len(self):
        return self.header.data_len

    @property
    def end_guard(self):
        return self.header.end_guard

    @property
    def name(self):
        name_len = self.header.name_len
        return self.rest_name[:name_len]

    @property
    def data(self):
        data_len = self.header.data_len
        if data_len:
            return self.rest_data[:data_len]
        else:
            return None

    def __eq__(self, other):
        return _same_message_struct(self, other)

    def __ne__(self, other):
        return not _same_message_struct(self, other)

    def equivalent(self, other):
        return _equivalent_message_struct(self, other)

# Is this premature optimisation?
# I don't think Python would cache the different classes for me,
# and it seems wasteful to create a new class for *every* message,
# given there will be a lot of messages that are very similar...
_specific_entire_message_struct_dict = {}

def _specific_entire_message_struct(padded_name_len, padded_data_len):
    """Return a specific subclass of _MessageHeaderStruct
    """
    key = (padded_name_len, padded_data_len)
    if key in _specific_entire_message_struct_dict:
        return _specific_entire_message_struct_dict[key]
    else:
        class localEntireMessageStruct(_EntireMessageStructBaseclass):
            _fields_ = [('header',     _MessageHeaderStruct),
                        ('rest_name',  ctypes.c_char  * padded_name_len),
                        ('rest_data',  ctypes.c_uint8 * padded_data_len),
                        ('rest_end_guard',  ctypes.c_uint32)]
        _specific_entire_message_struct_dict[key] = localEntireMessageStruct
        return localEntireMessageStruct

def _entire_message_from_parts(id, in_reply_to, to, from_, orig_from, final_to,
                               flags, name, data, encoding="utf-8", errors="strict"):
    """Return a new message structure of the correct shape.

    - 'id' and 'in_reply_to' are None or MessageId instance or (network_id,
      serial_num) tuples
    - 'to', 'in_reply_to' and 'from_' are 0 or a Ksock id
    - 'orig_from' and 'final_to' are None or OrigFrom instance or (network_id,
      local_id) tuple
    - 'name' is a (UTF-8) string
    - 'data' is a string, bytes or None
    - 'encoding' and 'errors' are the parameters used to encode if 'data' is
      a string

    Note that the result may be slightly longer than you expect - for instance,
    on a 64-bit machine, there will be 4 bytes of padding after the final
    end guard.
    """

    if id is None:
        id = MessageId(0,0)

    if in_reply_to is None:
        in_reply_to = MessageId(0,0)

    if orig_from is None:
        orig_from = OrigFrom(0,0)

    if final_to is None:
        final_to = OrigFrom(0,0)

    name = bytes(name, encoding="utf-8")
    name_len = len(name)

    if data is None:
        data = b''
    elif isinstance(data, str):
        data = data.encode(encoding=encoding, errors=errors)

    data_len = len(data)

    # C wants us to have a terminating 0 byte
    name += b'\0'
    # And we want to pad the result out to a multiple of 4 bytes
    # ...this is about the nastiest way possible of doing it...
    while len(name)%4:
        name += b'\0'

    padded_name_len = len(name)

    # We want to pad the data out in the same manner
    # (but without the terminating 0 byte)
    while len(data)%4:
        data += b'\0'
    padded_data_len = len(data)

    header = _MessageHeaderStruct(Message.START_GUARD,
                                  id, in_reply_to,
                                  to, from_, orig_from, final_to, 0, flags,
                                  name_len, data_len,
                                  None, None, Message.END_GUARD)

    DataArray = ctypes.c_uint8 * padded_data_len
    data_array = DataArray( *data )

    # We rather rely on 'data' "disappearing" (being of zero length)
    # if 'data_len' is zero, and it appears that that just works.

    local_class = _specific_entire_message_struct(padded_name_len,
                                                  padded_data_len)
    return local_class(header, name, data_array, Message.END_GUARD)

def _entire_message_from_bytes(data):
    """Return a message structure based on 'data'.

    'data' is a string-like object (as, for instance, returned by 'read')

    Note that the result may be slightly longer than you expect - for instance,
    on a 64-bit machine, there will be 4 bytes of padding after the final
    end guard.
    """
    # We do *not* want to pass something awful to our C-structure factory!
    if len(data) < MSG_HEADER_LEN:
        raise ValueError('Cannot form entire message from string'
                         ' "%s" of length %d'%(hexdata(data),len(data)))
    if struct.unpack('=L',data[:4])[0] != Message.START_GUARD:
        raise ValueError('Cannot form entire message from string "%s..%s"'
                         ' which does not start with message start'
                         ' guard'%(hexdata(data[:8]),hexdata(data[-8:])))
    ## ===================================
    debug = False
    if debug:
        print()
        print('_entire_message_from_bytes(%d:%s)'%(len(data),hexify(data)))
    ## ===================================
    h = _struct_from_bytes(_MessageHeaderStruct, data)
    ## ===================================
    if debug:
        print('_MessageHeaderStruct: %s'%h)
    ## ===================================

    # Don't forget that the string will be terminated with a 0 byte
    padded_name_len = calc_padded_name_len(h.name_len)

    # But not so the data
    padded_data_len = calc_padded_data_len(h.data_len)

    local_class = _specific_entire_message_struct(padded_name_len,
                                                  padded_data_len)

    ## ===================================
    if debug:
        print('name_len %d -> %d, data_len %d -> %d'%(h.name_len, padded_name_len, h.data_len, padded_data_len))
        x = _struct_from_bytes(local_class, data)
        print('_specific_class:      %s'%x)
        print()
    ## ===================================

    return _struct_from_bytes(local_class, data)

class Message:
    r"""A wrapper for a KBUS message

    A Message can be created in a variety of ways. Perhaps most obviously:

        >>> msg = Message('$.Fred')
        >>> msg
        Message('$.Fred')

        >>> msg = Message('$.Fred', '1234')
        >>> msg
        Message('$.Fred', data=b'1234')

        >>> msg = Message('$.Fred', '12345678')
        >>> msg
        Message('$.Fred', data=b'12345678')

        >>> msg1 = Message('$.Fred', data=b'1234')
        >>> msg1
        Message('$.Fred', data=b'1234')

    A :class:`Message` can be constructed from another message directly:

        >>> msg2 = Message.from_message(msg1)
        >>> msg2 == msg1
        True

    or from the tuple returned by :meth:`.extract`:

        >>> msg3 = Message.from_sequence(msg1.extract())
        >>> msg3 == msg1
        True

    or from an equivalent list::

        >>> msg3 = Message.from_sequence(list(msg1.extract()))
        >>> msg3 == msg1
        True

    or one can use a "string" -- for instance, as returned by the
    :meth:`Ksock.read` method:

        >>> msg_as_string = msg1.to_bytes()
        >>> msg4 = Message.from_bytes(msg_as_string)
        >>> msg4 == msg1
        True

    Some testing is made on the first argument - a printable string must start
    with ``$.`` (KBUS itself will make a more stringent test when the message is
    sent):

        >>> Message('Fred')
        Traceback (most recent call last):
        ...
        ValueError: Message name "Fred" does not start "$."

    and a data "string" must be plausible - that is, long enough for the
    minimal message header:

        >>> Message.from_bytes(msg_as_string[:8])
        Traceback (most recent call last):
        ...
        ValueError: Cannot form entire message from string "Kbus\x00\x00\x00\x00" of length 8

    and starting with a message start guard:

        >>> Message.from_bytes(b'1234'+msg_as_string)
        Traceback (most recent call last):
        ...
        ValueError: Cannot form entire message from string "1234Kbus..1234subK" which does not start with message start guard

    When constructing a message from another message, one may override
    particular values (but not the name):

        >>> msg5 = Message.from_message(msg1, to=9, in_reply_to=MessageId(0, 3))
        >>> msg5
        Message('$.Fred', data=b'1234', to=9, in_reply_to=MessageId(0, 3))

        >>> msg5a = Message.from_message(msg1, to=9, in_reply_to=MessageId(0, 3))
        >>> msg5a == msg5
        True

    However, whilst it is possible to set (for instance) `to` back to 0 by this method:

        >>> msg6 = Message.from_message(msg5, to=0)
        >>> msg6
        Message('$.Fred', data=b'1234', in_reply_to=MessageId(0, 3))

    (and the same for any of the integer fields), it is not possible to set any
    of the message id fields to None:

        >>> msg6 = Message.from_message(msg5, in_reply_to=None)
        >>> msg6
        Message('$.Fred', data=b'1234', to=9, in_reply_to=MessageId(0, 3))

    If you need to do that, go via the :meth:`extract()` method:

        >>> (id, in_reply_to, to, from_, orig_from, final_to, flags, name, data) = msg5.extract()
        >>> msg6 = Message(name, data, to, from_, None, None, flags, id)
        >>> msg6
        Message('$.Fred', data=b'1234', to=9)

    For convenience, the parts of a Message may be retrieved as properties:

        >>> print(msg1.id)
        None
        >>> msg1.name
        '$.Fred'
        >>> msg1.to
        0
        >>> msg1.from_
        0
        >>> print(msg1.in_reply_to)
        None
        >>> msg1.flags
        0
        >>> msg1.data
        b'1234'

    Message ids are objects if set:

        >>> msg1 = Message('$.Fred', data='1234', id=MessageId(0, 33))
        >>> msg1
        Message('$.Fred', data=b'1234', id=MessageId(0, 33))
        >>> msg1.id
        MessageId(0, 33)

    The arguments to Message() are:

    - `arg` -- this is the initial argument, and is a message name (a string
      that starts ``$.``), a Message, or a string representing an "entire"
      message.

            If `arg` is a message name, or another Message then the keyword
            arguments may be used (for another Message, they override the
            values in that Message).
            If `arg` is a message-as-a-string, any keyword arguments will be
            ignored.

    - `data` is data for the Message, either :const:`None`, a Python string
      or a Python bytes object.  A Python string will be encoded to bytes
      using the `encoding` and `errors` parameters below.
    - `to` is the Ksock id for the destination, for use in replies or in
      stateful messaging. Normally it should be left 0.
    - `from_` is the Ksock id of the sender. Normally this should be left
      0, as it is assigned by KBUS.
    - if `in_reply_to` is non-zero, then it is the Ksock id to which the
      reply shall go (taken from the `from_` field in the original message).
      Setting `in_reply_to` non-zero indicates that the Message *is* a reply.
      See also the Reply class, and especially the `reply_to` function, which
      makes constructing replies simpler.
    - `flags` can be used to set the flags for the message. If all that is
      wanted is to set the :const:`Message.WANT_A_REPLY` flag, it is
      simpler to use the :class:`Request` class to construct the message.
    - `id` may be used to set the message id, although unless the `network_id`
      field is
      set, KBUS will ignore this and set the id internally (this can be useful
      when constructing a message to compare received messages against).
    - `encoding` and `errors` are the parameters used when encoding if `data`
      is a string.

    Our internal values are:

    - `msg`, which is the actual message datastructure.

    .. note:: Message data is always held as the appropriate C datastructure
       (via ctypes), mainly to try to minimise copying of data in and out of
       that form.  A "pointy" or "entire" form is used as appropriate.
       
       The message fields (inside `msg`) are readable directly (as
       properties of :class:`Message`), but are not directly writable.
       Setter methods (:meth:`set_urgent` and :meth:`set_want_reply`)
       are provided for those which are likely to be sensible
       to alter in normal use.


       If you need to alter the Message contents, beyond use of the setter
       methods, then you will need to do so via the internal `msg`
       datastructure, with a clear understanding of the KBUS datastructure.
       If you need an example of doing this, see the Limpet codebase (which
       changes the `id`, `orig_from` and `final_to` fields, not something
       normal code should need or want to do).
    """

    START_GUARD = 0x7375624B
    END_GUARD   = 0x4B627573

    WANT_A_REPLY        = _BIT(0)
    WANT_YOU_TO_REPLY   = _BIT(1)
    SYNTHETIC           = _BIT(2)
    URGENT              = _BIT(3)

    ALL_OR_WAIT         = _BIT(8)
    ALL_OR_FAIL         = _BIT(9)

    def __init__(self, name, data=None, to=None, from_=None, orig_from=None,
                 final_to=None, in_reply_to=None, flags=None, id=None,
                 encoding="utf-8", errors="strict"):
        """Initialise a Message.
        """

        if not name.startswith('$.'):
            raise ValueError('Message name "%s" does not start "$."'%name)

        self._from_data(name, data, to, from_, orig_from, final_to,
                        in_reply_to, flags, id, encoding, errors)

        # Make sure the result *looks* like a message
        self._check()

    @staticmethod
    def from_message(msg, data=None, to=None, from_=None, orig_from=None,
                     final_to=None, in_reply_to=None, flags=None, id=None,
                     encoding="utf-8", errors="strict"):
        """Construct a :class:`Message` from another message.

        All the values in the old message, except the name, may be changed
        by specifying new values in the argument list.

        For instance:

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Message.from_message(msg1, flags=1)
            >>> msg2
            Message('$.Fred', data=b'12345678', flags=0x00000001)
        """
        message = Message.__new__(Message,'')
        message._merge_args(msg.extract(), data, to, from_, orig_from,
                            final_to, in_reply_to, flags, id, encoding, errors)
        return message

    @staticmethod
    def from_sequence(seq, data=None, to=None, from_=None, orig_from=None,
                      final_to=None, in_reply_to=None, flags=None, id=None,
                      encoding="utf-8", errors="strict"):
        """Construct a :class:`Message` from a sequence, as returned by `extract`.

        All the values in the old message, except the name, may be changed
        by specifying new values in the argument list.

        For instance:

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Message.from_sequence(msg1.extract(), flags=1)
            >>> msg2
            Message('$.Fred', data=b'12345678', flags=0x00000001)
        """
        if len(seq) != 9:
            raise ValueError("Sequence arg to Message.from_sequence() must have"
                    " 9 values, not %d"%len(seq))

        message = Message.__new__(Message,'')
        message._merge_args(seq, data, to, from_, orig_from,
                            final_to, in_reply_to, flags, id, encoding, errors)
        return message

    @staticmethod
    def from_bytes(arg):
        """Construct a :class:`Message` from bytes, as read by
        :meth:`Ksock.read_data`.

        For instance:

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Message.from_bytes(msg1.to_bytes())
            >>> msg2
            Message('$.Fred', data=b'12345678')
        """
        message = Message.__new__(Message,'')
        message.msg = _entire_message_from_bytes(arg)
        return message

    def _merge_args(self, extracted, this_data, this_to, this_from_,
                    this_orig_from, this_final_to, this_in_reply_to,
                    this_flags, this_id, encoding, errors):
        """Set our data from a msg.extract() tuple and optional arguments.

        Note that, if given, 'id' and 'in_reply_to' must be MessageId
        instances.

        Note that 'data' must be:

        1. a string, bytes, or something else compatible.
        2. None.

        A string will be encoded to bytes using the 'encoding' and 'errors'
        parameters.
        """
        (id, in_reply_to, to, from_, orig_from, final_to, flags, name, data) = extracted
        if this_data        is not None: data        = this_data
        if this_to          is not None: to          = this_to
        if this_from_       is not None: from_       = this_from_
        if this_orig_from   is not None: orig_from   = this_orig_from
        if this_final_to    is not None: final_to    = this_final_to
        if this_in_reply_to is not None: in_reply_to = this_in_reply_to
        if this_flags       is not None: flags       = this_flags
        if this_id          is not None: id          = this_id
        self._from_data(name, data, to, from_, orig_from, final_to, in_reply_to, flags, id, encoding, errors)

    def _from_data(self, name, data, to, from_, orig_from, final_to, in_reply_to, flags, id, encoding, errors):
        """Set our data from individual arguments.

        Note that, if given, 'id' and 'in_reply_to' must be MessageId
        instances, and 'orig_from' and 'final_to' OrigFrom instances.
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

        if orig_from:
            orig_from_tuple = (orig_from.network_id, orig_from.local_id)
        else:
            orig_from_tuple = (0, 0)

        if final_to:
            final_to_tuple = (final_to.network_id, final_to.local_id)
        else:
            final_to_tuple = (0, 0)

        if not flags:
            flags = 0

        self.msg = message_from_parts(id_tuple, in_reply_to_tuple,
                                      to, from_,
                                      orig_from_tuple, final_to_tuple,
                                      flags, name, data, encoding, errors)

    def _check(self):
        """Perform some basic sanity checks on our data.
        """
        if self.msg.start_guard != self.START_GUARD:
            raise ValueError("Message start guard is '%08x', not '%08x'"%\
                    (self.msg.start_guard, self.START_GUARD))
        if self.msg.end_guard != self.END_GUARD:
            raise ValueError("Message end guard is '%08x', not '%08x'"%\
                    (self.msg.end_guard, self.END_GUARD))
        if self.msg.name_len < 3:
            raise ValueError("Message name is %d long, minimum is 3"
                             " (e.g., '$.*')"%self.msg.name_len)

    def __repr__(self):
        (id, in_reply_to, to, from_, orig_from, final_to, flags, name, data) = self.extract()
        args = [repr(name)]
        if data is not None:
            args.append('data=%s'%repr(data))
        if to:
            args.append('to=%s'%repr(to))
        if from_:
            args.append('from_=%s'%repr(from_))
        if orig_from:
            args.append('orig_from=%s'%repr(orig_from))
        if final_to:
            args.append('final_to=%s'%repr(final_to))
        if in_reply_to:
            args.append('in_reply_to=%s'%repr(in_reply_to))
        if flags:
            args.append('flags=0x%08x'%flags)
        if id:
            args.append('id=%s'%repr(id))
        return 'Message(%s)'%(', '.join(args))

    def __str__(self):
        (id, in_reply_to, to, from_, orig_from, final_to, flags, name, data) = self.extract()
        # Try to be a bit friendly about what type of message this is
        if self.is_reply():
            if name.startswith('$.KBUS.'):
                what = 'Status'
            else:
                what = 'Reply'
        elif self.is_request():
            what = 'Request'
        else:
            what = 'Announcement'

        if name == '$.KBUS.ReplierBindEvent':
            what = 'ReplierBindEvent'
            parts = []
        else:
            parts = [repr(name)]

        if id:
            parts.append('id=%s'%str(id))
        if to:
            parts.append('to=%d'%to)
        if from_:
            parts.append('from=%d'%from_)
        if orig_from:
            parts.append('orig_from=%s'%str(orig_from))
        if final_to:
            parts.append('final_to=%s'%str(final_to))
        if in_reply_to:
            parts.append('in_reply_to=%s'%str(in_reply_to))
        if flags:
            txt = self._flag_text(flags)
            if txt:
                parts.append('flags=0x%x (%s)'%(flags,txt))
            else:
                parts.append('flags=0x%x'%flags)
        if data:
            if name == '$.KBUS.ReplierBindEvent':
                is_bind, binder_id, name = split_replier_bind_event_data(data)
                parts.append('[%s %s for %u]'%('Bind' if is_bind else 'Unbind',
                                               repr(name), binder_id))
            else:
                parts.append('data=%s'%repr(data))
        return '<%s %s>'%(what, ', '.join(parts))

    def _flag_text(self, flags):
        """A simple representation of the known flags.
        """
        words = []
        if flags & Message.WANT_A_REPLY:
            words.append('REQ')
        if flags & Message.WANT_YOU_TO_REPLY:
            words.append('YOU')
        if flags & Message.SYNTHETIC:
            words.append('SYN')
        if flags & Message.URGENT:
            words.append('URG')

        # I can't think of good short mnemonics for the next two,
        # so let's go with bad short mnemonics
        if flags & Message.ALL_OR_FAIL:
            words.append('aFL')
        if flags & Message.ALL_OR_WAIT:
            words.append('aWT')

        if len(words):
            return ','.join(words)
        else:
            return ''

    def __eq__(self, other):
        if not isinstance(other, Message):
            return False
        else:
            return (self.msg == other.msg)

    def __ne__(self, other):
        if not isinstance(other, Message):
            return True
        else:
            return (self.msg != other.msg)

    def total_length(self):
        """Return the total length of this message.

        A Message may be held in one of two ways:

        * "pointy" - this is a message header, with references to the
          message name and data.
        * "entire" - this is a message header with the message name
          and data (and an extra end guard) appended to it.

        Message construction may produce either of these (although
        construction of a message from a string will always produce
        an "entire" message). Reading a message from a :class:`Ksock` returns
        an "entire" message string.

        The actual "pointy" or "entire" message data is held in the
        `msg` value of the :class:`Message` instance.

        The :meth:`to_bytes` method returns the data for an "entire" message.
        In certain circumstances (typically, on a 64-byte system) the actual
        length of data returned by :meth:`to_bytes()` may be slightly too long
        (due to extra padding at the end).

        This method calculates the correct length of the equivalent "entire"
        message for this Message, without any such padding. If you want to
        write the data returned by :meth:`to_bytes()` into a :class:`Ksock`,
        only use the number of bytes indicated by this method.
        """
        return calc_entire_message_len(self.msg.name_len, self.msg.data_len)

    def equivalent(self, other):
        """Returns true if the two messages are mostly the same.

        For purposes of this comparison, we ignore
        `id`, `flags`, `in_reply_to` and `from_`.
        """
        return self.msg.equivalent(other.msg)

    def set_want_reply(self, value=True):
        """Set or unset the 'we want a reply' flag.
        """
        if value:
            self.msg.flags = _set_bit(self.msg.flags, Message.WANT_A_REPLY)
        else:
            self.msg.flags = _clear_bit(self.msg.flags, Message.WANT_A_REPLY)

    def set_urgent(self, value=True):
        """Set or unset the 'urgent message' flag.
        """
        if value:
            self.msg.flags = _set_bit(self.msg.flags, Message.URGENT)
        else:
            self.msg.flags = _clear_bit(self.msg.flags, Message.URGENT)

    def wants_us_to_reply(self):
        """Return :const:`True` if we (*specifically* us) are should reply to this message.
        """
        if self.msg.flags & Message.WANT_YOU_TO_REPLY:
            return True
        else:
            return False

    def is_synthetic(self):
        """Return :const:`True` if this is a synthetic message - one generated by KBUS.
        """
        if self.msg.flags & Message.SYNTHETIC:
            return True
        else:
            return False

    def is_urgent(self):
        """Return :const:`True` if this is an urgent message. """
        if self.msg.flags & Message.URGENT:
            return True
        else:
            return False

    @property
    def id(self):
        """ The `id` field of the message header. """
        network_id = self.msg.id.network_id
        serial_num = self.msg.id.serial_num
        if network_id == 0 and serial_num == 0:
            return None
        else:
            #return MessageId(network_id, serial_num)
            return self.msg.id

    @property
    def _id(self):
        """This is a "direct" form of the 'id' property.
        """
        return self.msg.id

    @property
    def in_reply_to(self):
        """ The `in_reply_to` field of the message header. """
        network_id = self.msg.in_reply_to.network_id
        serial_num = self.msg.in_reply_to.serial_num
        if network_id == 0 and serial_num == 0:
            return None
        else:
            #return MessageId(network_id, serial_num)
            return self.msg.in_reply_to

    @property
    def _in_reply_to(self):
        """This is a "direct" form of the 'in_reply_to' property.
        """
        return self.msg.in_reply_to

    @property
    def to(self):
        """ The `to` field of the message header. """
        return self.msg.to

    @property
    def from_(self):
        """ The `from` field of the message header. """
        return self.msg.from_

    @property
    def orig_from(self):
        """ The `orig_from` field of the message header. This is of type
        :class:`OrigFrom`. """
        network_id = self.msg.orig_from.network_id
        local_id   = self.msg.orig_from.local_id
        if network_id == 0 and local_id == 0:
            return None
        else:
            return self.msg.orig_from

    @property
    def _orig_from(self):
        """This is a "direct" version of the 'orig_from' property.
        """
        return self.msg.orig_from

    @property
    def final_to(self):
        """ The `final_to` field of the message header. This is of type
        :class:`OrigFrom`. """
        network_id = self.msg.final_to.network_id
        local_id   = self.msg.final_to.local_id
        if network_id == 0 and local_id == 0:
            return None
        else:
            return self.msg.final_to

    @property
    def _final_to(self):
        """This is a "direct" version of the 'final_to' property.
        """
        return self.msg.final_to

    @property
    def flags(self):
        """ The `flags` field of the message header. """
        return self.msg.flags

    @property
    def name(self):
        """ The `name` field of the message header.
        Any padding bytes are removed."""
        name_len = self.msg.name_len
        # Make sure we remove the padding bytes (although they *should* be
        # '\0', and so "reasonably safe")
        return str(self.msg.name[:name_len], encoding="utf-8")

    @property
    def data(self):
        """
        Returns the payload of this KBUS message as a Python bytes object,
        or None if it is not present.
        """
        if self.msg.data_len == 0:
            return None
        # To be friendly, return data as a Python (byte) string
        return c_data_as_bytes(self.msg.data, self.msg.data_len)

    def extract(self):
        """Return our parts as a tuple.

        The values are returned in something approximating the order
        within the message itself:

            (`id`, `in_reply_to`, `to`, `from_`, `orig_from`, `final_to`, 
            `flags`, `name`, `data`)

        This is not the same order as the keyword arguments to :meth:`Message`.
        """
        return (self.id, self.in_reply_to, self.to, self.from_, self.orig_from,
                self.final_to, self.flags, self.name, self.data)

    def to_bytes(self):
        """Return the message as a bytes object.

        This returns the entirety of the message as a Python string.

        In order to do this, it first coerces the mesage to an "entire"
        message (so that we don't have any dangling "pointers" to the
        name or data).

        See the :meth:`total_length` method for how to determine the "correct"
        length of this string.
        """
        (id, in_reply_to, to, from_, orig_from, final_to, flags, name, data) = self.extract()
        tmp = _entire_message_from_parts(id, in_reply_to, to, from_, orig_from,
                                         final_to, flags, name, data)
        b = _struct_to_bytes(tmp)
        # We need to be careful about lengths.
        # For instance, on a 64-bit machine, there will be 4 bytes of "unused"
        # padding after the final end guard of an "entire" message, which we
        # do not want to return. The solution is to return the correct,
        # calculated, length. Of course, this does cost us a string copy...
        name_len = len(name) if name else 0
        data_len = len(data) if data else 0
        actual_len = calc_entire_message_len(name_len, data_len)
        return b[:actual_len]

    def is_reply(self):
        """A convenience method - are we a Reply?
        """
        if self.in_reply_to:
            return True
        else:
            return False

    def is_request(self):
        """A convenience method - are we a Request?
        """
        if self.msg.flags & Message.WANT_A_REPLY:
            return True
        else:
            return False

    def is_stateful_request(self):
        """A convenience method - are we a Stateful Request?
        """
        if self.msg.flags & Message.WANT_A_REPLY and self.to:
            return True
        else:
            return False

    def cast(self):
        """Return (a copy of) ourselves as an appropriate subclass of 
        :class:`Message`.

        Reading from a :class:`Ksock` returns a :class:`Message`, whatever 
        the actual message type.
        Normally, this is OK, but sometimes it would be nice to have
        an actual message of the correct class.
        """
        if self.is_reply():
            # Status messages have a specific sort of name
            if self.msg.name.startswith(b'$.KBUS.'):
                return Status.from_bytes(self.to_bytes())
            else:
                return Reply.from_message(self)
        elif self.is_request():
            return Request.from_message(self)
        else:
            # Otherwise, it's basically an Announcement (at least, that's a good bet)
            return Announcement.from_message(self)

class Announcement(Message):
    """A "plain" message, needing no reply

    This is intended to be a convenient way of constructing a message that
    is just aimed at any listeners.

    It's also a terminological convenience - all of the "message" things are
    clearly messages, so we need a special name for "plain" messages...
    There's an argument for just factory functions to create these things,
    but a class feels a little cleaner to me.

    An Announcement can be created in a variety of ways. Perhaps most obviously:

        >>> ann1 = Announcement('$.Fred', data=b'1234')
        >>> ann1
        Announcement('$.Fred', data=b'1234')

    Since Announcement is a "plain" Message, we expect to be able to use the
    normal ways of instantiating a Message for an Announcement.

    So, an Announcement can be constructed from another message directly:

        >>> ann2 = Announcement.from_message(ann1)
        >>> ann2 == ann1
        True

        >>> msg = Announcement.from_message(ann1)
        >>> ann2a = Announcement.from_message(msg)
        >>> ann2 == ann2a
        True

    Since it's an Announcement, there's no `in_reply_to` argument

        >>> fail = Announcement('$.Fred', in_reply_to=None)
        Traceback (most recent call last):
        ...
        TypeError: __init__() got an unexpected keyword argument 'in_reply_to'

    and the `in_reply_to` value in Message objects is ignored:

        >>> msg = Message('$.Fred', data='1234', in_reply_to=MessageId(1, 2))
        >>> ann = Announcement.from_message(msg)
        >>> ann
        Announcement('$.Fred', data=b'1234')
        >>> print(ann.in_reply_to)
        None

    or from the :meth:`~Message.extract()` tuple - again, `reply_to` will be ignored:

        >>> ann3 = Announcement.from_sequence(ann1.extract())
        >>> ann3 == ann1
        True

    or from an equivalent list (and as above for `reply_to`):

        >>> ann3 = Announcement.from_sequence(list(ann1.extract()))
        >>> ann3 == ann1
        True

    Or one can use the same thing represented as a byte string:

        >>> ann_as_bytes = ann1.to_bytes()
        >>> ann4 = Announcement.from_bytes(ann_as_bytes)
        >>> ann4 == ann1
        True

    For convenience, the parts of an Announcement may be retrieved as properties:

        >>> print(ann1.id)
        None
        >>> ann1.name
        '$.Fred'
        >>> ann1.to
        0
        >>> ann1.from_
        0
        >>> print(ann1.in_reply_to) # always expected to be None
        None
        >>> ann1.flags
        0
        >>> ann1.data
        b'1234'

    Note that:

    1. An Announcement message is such because it is not a message of another
       type. There is nothing else special about it.

    """

    def __init__(self, name, data=None, to=None, from_=None, flags=None, id=None,
                 encoding="utf-8", errors="strict"):
        """Arguments are the same as for Message itself, absent 'in_reply_to'.
        """
        # Just do what the caller asked for directly
        super().__init__(name, data=data, to=to,
                         from_=from_, flags=flags, id=id,
                         encoding=encoding, errors=errors)
        # And, in case 'in_reply_to' got set by that
        self.msg.in_reply_to = MessageId(0, 0)
        # Or 'orig_from' and friend
        self.msg.orig_from = OrigFrom(0,0)
        self.msg.final_to = OrigFrom(0,0)

    @staticmethod
    def from_message(msg, data=None, to=None, from_=None, flags=None, id=None,
                     encoding="utf-8", errors="utf-8"):
        """Construct an Announcement from another message.

        The optional arguments allow changing the named fields in the new
        Announcement.

        For instance:

            >>> msg1 = Message('$.Fred', b'12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Announcement.from_message(msg1, flags=1)
            >>> msg2
            Announcement('$.Fred', data=b'12345678', flags=0x00000001)
        """
        message = Announcement.__new__(Announcement,'')
        message._merge_args(msg.extract(), data, to, from_, None, None, None,
                            flags, id, encoding, errors)
        # Just in case...
        message.msg.in_reply_to = MessageId(0, 0)
        message.msg.orig_from = OrigFrom(0,0)
        message.msg.final_to = OrigFrom(0,0)
        return message

    @staticmethod
    def from_sequence(seq, data=None, to=None, from_=None, flags=None, id=None,
                      encoding="utf-8", errors="strict"):
        """Construct an Announcement from a sequence, as returned by
        :meth:`~Message.extract`.

        The optional arguments allow changing the named fields in the new
        Announcement.

        For instance:

            >>> msg1 = Message('$.Fred', b'12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Announcement.from_sequence(msg1.extract(), flags=1)
            >>> msg2
            Announcement('$.Fred', data=b'12345678', flags=0x00000001)
        """
        if len(seq) != 9:
            raise ValueError("Sequence arg to Announcement.from_sequence() must have"
                    " 9 values, not %d"%len(seq))

        message = Announcement.__new__(Announcement,'')
        message._merge_args(seq, data, to, from_, None, None, None,
                            flags, id, encoding, errors)
        # Just in case...
        message.msg.in_reply_to = MessageId(0, 0)
        message.msg.orig_from = OrigFrom(0,0)
        message.msg.final_to = OrigFrom(0,0)
        return message

    @staticmethod
    def from_bytes(arg):
        """Construct an Announcement from bytes, as read by :meth:`Ksock.read_data`.

        For instance:

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Announcement.from_bytes(msg1.to_bytes())
            >>> msg2
            Announcement('$.Fred', data=b'12345678')
        """
        message = Announcement.__new__(Announcement,'')
        message.msg = _entire_message_from_bytes(arg)
        # Just in case...
        message.msg.in_reply_to = MessageId(0, 0)
        message.msg.orig_from = OrigFrom(0,0)
        message.msg.final_to = OrigFrom(0,0)
        return message

    def set_want_reply(self, value=True):
        """Announcements are not Requests.
        """
        raise TypeError("Announcements are not Requests")

    def __repr__(self):
        (id, in_reply_to, to, from_, orig_from, final_to, flags, name, data) = self.extract()
        args = [repr(name)]
        if data is not None:
            args.append('data=%s'%repr(data))
        if to:
            args.append('to=%s'%repr(to))
        if from_:
            args.append('from_=%s'%repr(from_))
        if flags:
            args.append('flags=0x%08x'%flags)
        if id:
            args.append('id=%s'%repr(id))
        return 'Announcement(%s)'%(', '.join(args))

class Request(Message):
    """A message that wants a reply.

    This is intended to be a convenient way of constructing a message that
    wants a reply.

    It doesn't take an `in_reply_to` initialisation argument:

        >>> fail = Request('$.Fred', in_reply_to=None)
        Traceback (most recent call last):
        ...
        TypeError: __init__() got an unexpected keyword argument 'in_reply_to'

    And it automatically sets the :const:`WANT_A_REPLY` flag, but otherwise it
    behaves just like a Message.

    For instance, consider:

        >>> msg = Message('$.Fred', data='1234', flags=Message.WANT_A_REPLY)
        >>> msg
        Message('$.Fred', data=b'1234', flags=0x00000001)
        >>> req = Request('$.Fred', data='1234')
        >>> req
        Request('$.Fred', data=b'1234', flags=0x00000001)
        >>> req == msg
        True

    If it is given a `to` argument, then it is a Stateful Request - it will be
    an error if it cannot be delivered to that particular Replier (for
    instance, if the Replier had unbound and someone else had bound as Replier
    for this message name).

        >>> req = Request('$.Fred', data=b'1234', to=1234)
        >>> req
        Request('$.Fred', data=b'1234', to=1234, flags=0x00000001)

    A Stateful Request may also need to supply a `final_to` argument, if the
    original Replier is over a (Limpet) network. This should be taken from an
    earlier Reply from that Replier -- see the convenience function
    stateful_request(). However, it can be done by hand:

        >>> req = Request('$.Fred', data=b'1234', to=1234, final_to=OrigFrom(12, 23), flags=0x00000001)
        >>> req
        Request('$.Fred', data=b'1234', to=1234, final_to=OrigFrom(12, 23), flags=0x00000001)

    Note that:

    1. A request message is a request just because it has the
       :const:`Message.WANT_A_REPLY` flag set. There is nothing else special
       about it.
    2. A stateful request message is then a request that has its `to` flag set.
    """

    def __init__(self, name, data=None, to=None, from_=None, final_to=None,
                 flags=None, id=None, encoding="utf-8", errors="strict"):
        """Arguments are exactly the same as for Message itself.
        """
        # First, just do what the caller asked for directly
        # but with 'in_reply_to' as 0
        super().__init__(name, data=data, to=to, from_=from_,
                         final_to=final_to, flags=flags, id=id,
                         encoding=encoding, errors=errors)
        # But then make sure that the "wants a reply" flag is set
        super().set_want_reply(True)

    @staticmethod
    def from_message(msg, data=None, to=None, from_=None, final_to=None,
                     flags=None, id=None, encoding="utf-8", errors="strict"):
        """Construct a Request from another message.

        The optional arguments allow changing the named fields in the new
        Request.

        For instance:

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Request.from_message(msg1, flags=2)
            >>> msg2
            Request('$.Fred', data=b'12345678', flags=0x00000003)
        """
        message = Request.__new__(Request,'')
        message._merge_args(msg.extract(), data, to, from_, None,
                            final_to, None, flags, id, encoding, errors)
        # But then make sure that the "wants a reply" flag is set
        super(Request, message).set_want_reply(True)
        return message

    @staticmethod
    def from_sequence(seq, data=None, to=None, from_=None, final_to=None,
                      flags=None, id=None, encoding="utf-8", errors="strict"):
        """Construct a Request from a sequence, as returned by
        :meth:`~Message.extract`.

        The optional arguments allow changing the named fields in the new
        Request.

        For instance:

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Request.from_sequence(msg1.extract(), flags=2)
            >>> msg2
            Request('$.Fred', data=b'12345678', flags=0x00000003)
        """
        if len(seq) != 9:
            raise ValueError("Sequence arg to Request.from_sequence() must have"
                    " 9 values, not %d"%len(seq))

        message = Request.__new__(Request,'')
        message._merge_args(seq, data, to, from_, None,
                            final_to, None, flags, id, encoding, errors)
        # But then make sure that the "wants a reply" flag is set
        super(Request, message).set_want_reply(True)
        return message

    @staticmethod
    def from_bytes(arg):
        """Construct a Request from bytes, as read by :meth:`Ksock.read_data`.

        For instance:

            >>> msg1 = Message('$.Fred', b'12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Request.from_bytes(msg1.to_bytes())
            >>> msg2
            Request('$.Fred', data=b'12345678', flags=0x00000001)
        """
        message = Request.__new__(Request,'')
        message.msg = _entire_message_from_bytes(arg)
        # But then make sure that the "wants a reply" flag is set
        super(Request, message).set_want_reply(True)
        return message

    def __repr__(self):
        (id, in_reply_to, to, from_, orig_from, final_to, flags, name, data) = self.extract()
        args = [repr(name)]
        if data is not None:
            args.append('data=%s'%repr(data))
        if to:
            args.append('to=%s'%repr(to))
        if from_:
            args.append('from_=%s'%repr(from_))
        if orig_from:
            args.append('orig_from=%s'%repr(orig_from))
        if final_to:
            args.append('final_to=%s'%repr(final_to))
        if flags:
            args.append('flags=0x%08x'%flags)
        if id:
            args.append('id=%s'%repr(id))
        return 'Request(%s)'%(', '.join(args))

    def set_want_reply(self):
        """ Calling this method is an error in this subclass, as by 
        definition a :class:`Request` always has the :const:`WANT_A_REPLY`
        flag set.  """
        raise TypeError('Request always has "want a reply" set')

class Reply(Message):
    """A reply message.

        (Note that the constructor for this class does *not* flip fields (such
        as `id` and `in_reply_to`, or `from_` and `to`) when building the Reply
        - if you want that behaviour (and you probably do), use the
        :func:`reply_to` function.)

    Thus Reply can be used as, for instance:

        >>> direct = Reply('$.Fred', to=27, in_reply_to=MessageId(0, 132))
        >>> direct
        Reply('$.Fred', to=27, in_reply_to=MessageId(0, 132))
        >>> reply = Reply.from_message(direct)
        >>> direct == reply
        True

    Since a Reply is a Message with its `in_reply_to` set, this *must* be provided:

        >>> msg = Message('$.Fred', data='1234', from_=27, to=99, id=MessageId(0, 132), flags=Message.WANT_A_REPLY)
        >>> msg
        Message('$.Fred', data=b'1234', to=99, from_=27, flags=0x00000001, id=MessageId(0, 132))
        >>> reply = Reply.from_message(msg)
        Traceback (most recent call last):
        ...
        ValueError: A Reply must specify in_reply_to

        >>> reply = Reply.from_message(msg, in_reply_to=MessageId(0, 5))
        >>> reply
        Reply('$.Fred', data=b'1234', to=99, from_=27, in_reply_to=MessageId(0, 5), flags=0x00000001, id=MessageId(0, 132))

    When Limpet networks are in use, it may be necessary to construct a Reply
    with its `orig_from` field set (this should only really be done by a Limpet
    itself, though):

        >>> reply = Reply.from_message(msg, in_reply_to=MessageId(0, 5), orig_from=OrigFrom(23, 92))
        >>> reply
        Reply('$.Fred', data=b'1234', to=99, from_=27, orig_from=OrigFrom(23, 92), in_reply_to=MessageId(0, 5), flags=0x00000001, id=MessageId(0, 132))

    It's also possible to construct a Reply in most of the other ways a
    :class:`Message` can be constructed. For instance:

        >>> rep2 = Reply.from_bytes(direct.to_bytes())
        >>> rep2 == direct
        True
        >>> rep4 = Reply.from_sequence(direct.extract())
        >>> rep4 == direct
        True
    """

    def __init__(self, name, data=None, to=None, from_=None, orig_from=None,
                 in_reply_to=None, flags=None, id=None, encoding="utf-8", errors="strict"):
        """Just do what the user asked, but they must give 'in_reply_to'.
        """

        super().__init__(name, data=data, to=to, from_=from_,
                         orig_from=orig_from,
                         in_reply_to=in_reply_to, flags=flags,
                         id=id, encoding=encoding, errors=errors)
        if self.in_reply_to is None:
            raise ValueError("A Reply must specify in_reply_to")

    @staticmethod
    def from_message(msg, data=None, to=None, from_=None, orig_from=None,
                     in_reply_to=None, flags=None, id=None,
                     encoding="utf-8", errors="strict"):
        """Construct a Message from another message.

        All the values in the old message, except the name, may be changed
        by specifying new values in the argument list.

        `in_reply_to` must be specified explicitly, if it is not present
        in the old/template message.

        For instance:

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Reply.from_message(msg1, flags=2, in_reply_to=MessageId(0,5))
            >>> msg2
            Reply('$.Fred', data=b'12345678', in_reply_to=MessageId(0, 5), flags=0x00000002)
        """
        message = Reply.__new__(Reply,'')
        message._merge_args(msg.extract(), data, to, from_, orig_from,
                            None, in_reply_to, flags, id, encoding, errors)
        if message.in_reply_to is None:
            raise ValueError("A Reply must specify in_reply_to")
        return message

    @staticmethod
    def from_sequence(seq, data=None, to=None, from_=None, orig_from=None,
                      in_reply_to=None, flags=None, id=None,
                      encoding="utf-8", errors="strict"):
        """Construct a Message from a sequence, as returned by
        :meth:`~Message.extract`.

        All the values in the old message, except the name, may be changed
        by specifying new values in the argument list.

        `in_reply_to` must be specified explicitly, if it is not present
        in the sequence.

        For instance:

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Reply.from_sequence(msg1.extract(), flags=2, in_reply_to=MessageId(0,5))
            >>> msg2
            Reply('$.Fred', data=b'12345678', in_reply_to=MessageId(0, 5), flags=0x00000002)
        """
        if len(seq) != 9:
            raise ValueError("Sequence arg to Message.from_sequence() must have"
                    " 9 values, not %d"%len(seq))

        message = Reply.__new__(Reply,'')
        message._merge_args(seq, data, to, from_, orig_from, None,
                            in_reply_to, flags, id, encoding, errors)
        if message.in_reply_to is None:
            raise ValueError("A Reply must specify in_reply_to")
        return message

    @staticmethod
    def from_bytes(arg):
        """Construct a Message from bytes, as read by
        :meth:`Ksock.read_data`.

        `in_reply_to` must be set in the message data.

        For instance:

            >>> msg1 = Message('$.Fred', b'12345678', in_reply_to=MessageId(0,5))
            >>> msg1
            Message('$.Fred', data=b'12345678', in_reply_to=MessageId(0, 5))
            >>> msg2 = Message.from_bytes(msg1.to_bytes())
            >>> msg2
            Message('$.Fred', data=b'12345678', in_reply_to=MessageId(0, 5))
        """
        message = Reply.__new__(Reply,'')
        message.msg = _entire_message_from_bytes(arg)
        if message.in_reply_to is None:
            raise ValueError("A Reply must specify in_reply_to")
        return message

    def __repr__(self):
        (id, in_reply_to, to, from_, orig_from, final_to, flags, name, data) = self.extract()
        args = [repr(name)]
        if data is not None:
            args.append('data=%s'%repr(data))
        if to:
            args.append('to=%s'%repr(to))
        if from_:
            args.append('from_=%s'%repr(from_))
        if orig_from:
            args.append('orig_from=%s'%repr(orig_from))
        if in_reply_to:
            args.append('in_reply_to=%s'%repr(in_reply_to))
        if flags:
            args.append('flags=0x%08x'%flags)
        if id:
            args.append('id=%s'%repr(id))
        return 'Reply(%s)'%(', '.join(args))

class Status(Message):
    """A status message, from KBUS.

    This is provided as a sugar-coating around the messages KBUS sends us. As
    such, it is not expected that a normal user would want to construct one,
    and the initialisation mechanisms are correspondingly more restrictive.

    For instance:

        >>> msg = Message('$.KBUS.Dummy', from_=27, to=99, in_reply_to=MessageId(0, 132))
        >>> msg
        Message('$.KBUS.Dummy', to=99, from_=27, in_reply_to=MessageId(0, 132))
        >>> status = Status.from_bytes(msg.to_bytes())
        >>> status
        Status('$.KBUS.Dummy', to=99, from_=27, in_reply_to=MessageId(0, 132))

    At the moment it is not possible to construct a Status message in any other
    way - it is assumed to be strictly for "wrapping" a message read (as bytes)
    from KBUS. Thus:

        >>> msg = Status('$.Fred')
        Traceback (most recent call last):
        ...
        NotImplementedError: Use the Status.from_bytes() method to construct a Status

    Note that:

    1. A status message is such because it is a (sort of) 
       :class:`Reply`, with the
       message name starting with ``$.KBUS.``.
    """

    def __init__(self, original):
        raise NotImplementedError('Use the Status.from_bytes() method to construct a Status')

    @staticmethod
    def from_message(msg, data=None, to=None, from_=None, orig_from=None,
                     final_to=None, in_reply_to=None, flags=None, id=None,
                     encoding="utf-8", errors="strict"):
        """It is not meaningful to create a Status from another Message.

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Status.from_message(msg1, in_reply_to=MessageId(0,5))
            Traceback (most recent call last):
            ...
            NotImplementedError: Status does not support the from_message() static method
        """
        raise NotImplementedError('Status does not support the from_message() static method')

    @staticmethod
    def from_sequence(seq, data=None, to=None, from_=None, orig_from=None,
                      final_to=None, in_reply_to=None, flags=None, id=None,
                      encoding="utf-8", errors="strict"):
        """It is not meaningful to create a Status from a sequence.

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Status.from_sequence(msg1.extract())
            Traceback (most recent call last):
            ...
            NotImplementedError: Status does not support the from_sequence() static method
        """
        raise NotImplementedError('Status does not support the from_sequence() static method')

    @staticmethod
    def from_bytes(arg):
        """Construct a Status from bytes, as read by :meth:`Ksock.read_data`.

        For instance:

            >>> msg1 = Message('$.Fred', '12345678')
            >>> msg1
            Message('$.Fred', data=b'12345678')
            >>> msg2 = Status.from_bytes(msg1.to_bytes())
            >>> msg2
            Status('$.Fred', data=b'12345678')
        """
        message = Status.__new__(Status,'')
        message.msg = _entire_message_from_bytes(arg)
        return message

    def __repr__(self):
        (id, in_reply_to, to, from_, orig_from, final_to, flags, name, data) = self.extract()
        args = [repr(name)]
        if data is not None:
            args.append('data=%s'%repr(data))
        if to:
            args.append('to=%s'%repr(to))
        if from_:
            args.append('from_=%s'%repr(from_))
        if in_reply_to:
            args.append('in_reply_to=%s'%repr(in_reply_to))
        if flags:
            args.append('flags=0x%08x'%flags)
        if id:
            args.append('id=%s'%repr(id))
        return 'Status(%s)'%(', '.join(args))

def reply_to(original, data=None, flags=0, encoding="utf-8", errors="strict"):
    """Return a :class:`Reply` to the given :class:`Message`.

    This is intended to be the normal way of constructing a reply message.

    For instance:

        >>> msg = Message('$.Fred', data=b'1234', from_=27, to=99, id=MessageId(0, 132), flags=Message.WANT_A_REPLY|Message.WANT_YOU_TO_REPLY)
        >>> msg
        Message('$.Fred', data=b'1234', to=99, from_=27, flags=0x00000003, id=MessageId(0, 132))
        >>> reply = reply_to(msg)
        >>> reply
        Reply('$.Fred', to=27, in_reply_to=MessageId(0, 132))

    Note that:

    1. The message we're constructing a reply to must be a message that wants
       a reply. Specifically, this means that it must have the
       :const:`Message.WANT_A_REPLY` flag set, and also the :const:`Message.WANT_YOU_TO_REPLY`
       flag. This last is because anyone listening to a Request will "see" the
       :const:`Message.WANT_A_REPLY` flag,
       but only the (single) replier will receive the message with 
       :const:`Message.WANT_YOU_TO_REPLY` set.
    2. A reply message is a reply because it has the `in_reply_to` field set.
       This indicates the message id of the original message, the one we're
       replying to.
    3. As normal, the Reply's own message id is unset - KBUS will set this, as
       for any message.
    4. We give a specific `to` value, the id of the :class:`Ksock` that sent
       the original message, and thus the `from` value in the original message.
    5. We keep the same message name, but don't copy the original message's
       data. If we want to send data in a reply message, it will be our own
       data.

    The other arguments available are `flags` (allowing the setting of flags
    such as :const:`Message.ALL_OR_WAIT`, for instance), and `data`,
    allowing reply data to be added:

        >>> rep4 = reply_to(msg, flags=Message.ALL_OR_WAIT, data='1234')
        >>> rep4
        Reply('$.Fred', data=b'1234', to=27, in_reply_to=MessageId(0, 132), flags=0x00000100)
    """

    # Check we're allowed to reply to this
    if original.flags & (Message.WANT_A_REPLY | Message.WANT_YOU_TO_REPLY) != \
            Message.WANT_A_REPLY | Message.WANT_YOU_TO_REPLY:
        raise ValueError("Cannot form a reply to a message that does not have"
                " WANT_A_REPLY and WANT_YOU_TO_REPLY set: %s"%original)

    (id, in_reply_to, to, from_, orig_from, final_to, original_flags,
            name, data_tuple) = original.extract()
    # We reply to the original sender (to), indicating which message we're
    # responding to (in_reply_to).
    #
    # The fact that in_reply_to is set means that we *are* a reply.
    #
    # We don't need to set any flags. We definitely *don't* want to copy
    # any flags from the original message.
    return Reply(name, data=data, in_reply_to=id, to=from_, flags=flags,
                 encoding=encoding, errors=errors)

def stateful_request(earlier_msg, name, data=None, from_=None,
                     flags=None, id=None, encoding="utf-8", errors="strict"):
    """Construct a stateful Request, based on an earlier :class:`Reply` or
    stateful :class:`Request`.

    This is intended to be the normal way of constructing a stateful request.

    `earlier_msg` is either:

    1. an earlier Reply, whose `from_` field will be used as the new Request's
       `to` field, and whose `orig_from` field will be used as the new Request's
       `final_to` field.

            Remember, a Reply is a message whose `in_reply_to` field is set.

    2. an earlier Stateful Request, whose `to` and `orig_from` fields will be
       copied to the new Request.

            Remember, a Stateful Request is a message with the
            :const:`Message.WANT_A_REPLY`
            flag set (a Request), and whose `to` field is set (which is to a
            specific Replier).

    The rest of the arguments are the same as for :meth:`Request`, except that the
    `to` and `orig_from` initialiser arguments are missing.

    For instance, in the normal (single network) case:

        >>> reply = Reply('$.Fred', to=27, from_=39, in_reply_to=MessageId(0, 132))
        >>> reply
        Reply('$.Fred', to=27, from_=39, in_reply_to=MessageId(0, 132))
        >>> request = stateful_request(reply, '$.SomethingElse')
        >>> request
        Request('$.SomethingElse', to=39, flags=0x00000001)

    or, with a Reply that has come from far away:

        >>> reply = Reply('$.Fred', to=27, from_=39, in_reply_to=MessageId(0, 132), orig_from=OrigFrom(19,23))
        >>> reply
        Reply('$.Fred', to=27, from_=39, orig_from=OrigFrom(19, 23), in_reply_to=MessageId(0, 132))
        >>> request = stateful_request(reply, '$.SomethingElse')
        >>> request
        Request('$.SomethingElse', to=39, final_to=OrigFrom(19, 23), flags=0x00000001)

    or, reusing our stateful Request:

        >>> request = stateful_request(request, '$.Again', data='Aha!')
        >>> request
        Request('$.Again', data=b'Aha!', to=39, final_to=OrigFrom(19, 23), flags=0x00000001)
    """
    if earlier_msg.is_reply():
        final_to = earlier_msg.orig_from
        to = earlier_msg.from_
    elif earlier_msg.is_stateful_request():
        final_to = earlier_msg.final_to
        to = earlier_msg.to
    else:
        raise ValueError("The first argument of stateful_request() must be a"
                         " Reply or a previous Stateful Request")

    return Request(name, data=data, to=to, from_=from_, final_to=final_to,
                   flags=flags, id=id, encoding=encoding, errors=errors)


class _ReplierBindEventHeader(ctypes.Structure):
    """The "header" part of a ``$.KBUS.ReplierBindEvent`` message
    """
    _fields_ = [('is_bind', ctypes.c_uint32),
                ('binder',  ctypes.c_uint32),
                ('name_len',ctypes.c_uint32)]

def split_replier_bind_event_data(data):
    """Split the data from a ``$.KBUS.ReplierBindEvent`` message.

    Returns a tuple of the form (is_bind, binder, name)
    """

    hdr = _struct_from_bytes(_ReplierBindEventHeader, data)

    offset = ctypes.sizeof(_ReplierBindEventHeader)

    name = data[offset:offset+hdr.name_len].decode()

    return (hdr.is_bind, hdr.binder, name)

if __name__ == "__main__":
    import doctest
    doctest.testmod()

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
