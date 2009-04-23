from __future__ import with_statement

import os, time
import errno
from kbus import *
from test_kbus import check_IOError
import array

name = '$.Fred'
data = '12345678'

def test_building_header(self):
    """Test building a header from scratch.
    """
    name = '$.Fred'
    data = '12345678'
    header = KbusMessageHeaderStruct(Message.START_GUARD,
                                     KbusMessageIdStruct(0,0),
                                     KbusMessageIdStruct(0,27),
                                     32,
                                     0,
                                     0,
                                     len(name),
                                     len(data)/4)
    #print 'header', header
    assert header.start_guard == Message.START_GUARD
    assert header.id.network_id == 0
    assert header.id.serial_num == 0
    assert header.in_reply_to.network_id == 0
    assert header.in_reply_to.serial_num == 27
    assert header.to == 32
    assert header.from_ == 0
    assert header.flags == 0
    assert header.name_len == len(name)
    assert header.data_len == len(data)/4

    # and whilst we're at it
    import kbus
    strhdr = kbus._struct_to_string(header)
    #print 'As string:',
    #for char in strhdr:
    #    print ' %02x'%ord(char),
    #print

    h2 = kbus._struct_from_string(KbusMessageHeaderStruct,strhdr)
    #print 'h2',h2

    assert h2==header

    empty = KbusMessageHeaderStruct()
    #print 'empty', empty
    assert empty.start_guard == 0
    assert empty.id.network_id == 0
    assert empty.id.serial_num == 0
    assert empty.in_reply_to.network_id == 0
    assert empty.in_reply_to.serial_num == 0
    assert empty.to == 0
    assert empty.from_ == 0
    assert empty.flags == 0
    assert empty.name_len == 0
    assert empty.data_len == 0

def test_building_entire_message(self):
    """Some more testing of building messages in odd ways.
    """
    name = '$.Fred'
    data = (1234,5678)
    header = KbusMessageHeaderStruct(Message.START_GUARD,
                                     KbusMessageIdStruct(0,0),
                                     KbusMessageIdStruct(0,27),
                                     32,
                                     0,
                                     0,
                                     len(name),
                                     len(data)/4)
    import kbus
    strhdr = kbus._struct_to_string(header)
    end_guard = array.array('L',[Message.END_GUARD]).tostring()

    fred = entire_message_from_string(strhdr + name+'  ' + data + end_guard)
    assert fred.start_guard == Message.START_GUARD
    assert fred.id.network_id == 0
    assert fred.id.serial_num == 0
    assert fred.in_reply_to.network_id == 0
    assert fred.in_reply_to.serial_num == 27
    assert fred.to == 32
    assert fred.from_ == 0
    assert fred.flags == 0
    assert fred.name_len == len(name)
    assert fred.data_len == len(data)/4
    assert fred.name == '$.Fred'
    assert fred.data[0] == 1234
    assert fred.data[1] == 5678

    #print 'fred', fred

def aschr(word):
    return '%c%c%c%c'%(chr(word & 0xFF),
            chr((word >> 8) & 0xFF),
            chr((word >> 16) & 0xFF),
            chr((word >> 24) & 0xFF))

def test_odd_message_creation(self):
    """Test some of the more awkward ways of making messages.
    """
    simple = NewMessage('$.Jim.Bob',data=(1,2,3,4,5,6),to=32,
                        in_reply_to=MessageId(0,27))
    #print simple
    #print simple.msg

    latest = entire_message_from_parts((0,0), (0,27), 32, 0, 0, '$.Jim.Bob',
                                       (1,2,3,4,5,6))
    #print latest
    assert latest == simple.msg

    # For added ickiness
    latest = entire_message_from_parts((0,0), (0,27), 32, 0, 0, '$.Jim.Bob',
                                       tuple(array.array('L',(1,2,3,4,5,6)).tolist()))
    #print latest
    assert latest == simple.msg

def test_message_comparisons(self):
    """Tests comparing equality of two messages.
    """
    a = NewMessage('$.Fred')
    #print a
    b = NewMessage('$.Fred')
    #print b
    assert a.msg.name == b.msg.name
    #print a.msg.header
    #print b.msg.header
    assert a.msg.header == b.msg.header

    #import kbus
    #print kbus._int_tuple_as_str(a.msg.data)
    #print kbus._int_tuple_as_str(b.msg.data)

    # The 'data' field is a (ctypes) array of integers, and
    # as such they don't compare with each other directly in
    # any useful manner. So we know that doing::
    #
    #    assert a.msg.data == b.msg.data
    #
    # would fail (and not really tell us anything). However,
    # the message datastructure itself knows how to compare
    # its own innards, including said data field, so:
    assert a.msg == b.msg
    # And at the next level up, we can do the same thing:
    assert a == b

    # And with some data
    c = NewMessage('$.JimBob',(0x1234,0x56780000))
    d = NewMessage('$.JimBob',(0x1234,0x56780000))
    assert c.msg == d.msg
    assert c == d

    # And for completeness
    assert a == a
    assert c == c
    assert not (a != b)
    assert not (c != d)
    assert a != c

def test_message_length(self):
    """Test that message size is what we expect.

    This checks that the underlying ctypes structure is the size we want.

    - The header is 40 bytes (10*4).
    - Message names get rounded up to the nearest 4 bytes, with no
      0 terminator
    - Message data is in units of 4 bytes
    - The end guard is 4 bytes

    ...so all messages are 44 bytes + message-name-rounded + data
    """
    # Minimal message length
    assert NewMessage('$.F').size == (44+4)
    # Testing the rounding of message name
    assert NewMessage('$.Fr').size == (44+4)
    assert NewMessage('$.Fre').size == (44+8)
    assert NewMessage('$.Fred').size == (44+8)
    assert NewMessage('$.Fredd').size == (44+8)
    assert NewMessage('$.Freddy').size == (44+8)
    assert NewMessage('$.Freddys').size == (44+12)

test_building_header(0)
test_message_comparisons(0)
test_odd_message_creation(0)
test_message_length(0)

#os.system('sudo insmod kbus.ko')
#time.sleep(0.5)

#try:

#finally:
#    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
