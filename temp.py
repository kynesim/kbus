from __future__ import with_statement

import os, time
import errno
from kbus import *
from test_kbus import check_IOError

from kbus import _struct_from_string
from kbus import _MessageHeaderStruct

os.system('sudo insmod kbus.ko')
time.sleep(0.5)

def hexify_data(data):
    """Return a representation of a 'string' as hex values.
    """
    words = []
    for ch in data:
        words.append('%02x'%ord(ch))
    return ' '.join(words)

def test_message(sender, listener, description, message):
    print '%s: %s'%(description, message)

    sender.write_data(message)
    sender.send()

    length = listener.next_msg()
    print '  Next message has length',length

    data = listener.read_data(length)

    #print hexify_data(data1)

    entire = entire_message_from_string(data)
    print '  Entire message read:',entire

    print '  Equivalent?', message.equivalent(entire)

    m = message_from_string(data)
    print '  Same data as plain message:',m

    print '  Equivalent?', message.equivalent(m)

    return entire

def run_test(sender, listener):
    name = '$.Fred'
    data = 'somedata'
    name_ptr = ctypes.c_char_p(name)
    data_ptr = ctypes.c_char_p(data)

    # Without data
    pointy1 = _MessageHeaderStruct(Message.START_GUARD,
                                   MessageId(0,0),
                                   MessageId(0,0),
                                   0,
                                   0,
                                   0,
                                   6,
                                   0,
                                   name_ptr,
                                   None,
                                   Message.END_GUARD)

    entire1 = test_message(sender, listener, 'Pointy, no data', pointy1)

    # With data
    pointy2 = _MessageHeaderStruct(Message.START_GUARD,
                                   MessageId(0,0),
                                   MessageId(0,0),
                                   0,
                                   0,
                                   0,
                                   6,
                                   8,
                                   name_ptr,
                                   data_ptr,
                                   Message.END_GUARD)

    entire2 = test_message(sender, listener, 'Pointy, with data', pointy2)

    # The data back from the first two messages represent entire messages
    entire3 = test_message(sender, listener, 'Entire, no data', entire1)
    entire4 = test_message(sender, listener, 'Entire, with data', entire2)

try:
    with KSock(0,'rw') as sender:
        with KSock(0,'rw') as listener:
            listener.bind('$.Fred')
            #run_test(sender, listener)

            m = Message('$.Fred')
            print m
            print m.msg
            print type(m.msg)
            print m.msg.name
            print type(m.msg.name)
            #listener.send_msg(m)
finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
