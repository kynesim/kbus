#! /usr/bin/env python

from __future__ import with_statement

import os, time
import errno
from kbus import *
from kbus.test.test_kbus import check_IOError

from kbus.messages import _struct_from_string
from kbus.messages import _MessageHeaderStruct

os.system('sudo insmod ../kbus/kbus.ko')
time.sleep(0.5)


try:

        with Ksock(0) as f0:
            #f0.kernel_module_verbose()
            with Ksock(0) as f1:
                f1.bind('$.Fred')

                m = Message('$.Fred', '\x11\x22\x33\x44',
                            id=MessageId(3,5))
                m.msg.extra = 99
                print m

                # We can do it all in one go (the convenient way)
                f0.send_msg(m)
                r = f1.read_next_msg()
                assert r.equivalent(m)
                assert f1.next_msg() == 0

                # We can do it in two parts, but writing the whole message
                f0.write_msg(m)
                # Nothing sent yet
                assert f1.next_msg() == 0
                f0.send()
                r = f1.read_next_msg()
                assert r.equivalent(m)
                assert f1.next_msg() == 0

                # Or we can write our messsage out in convenient pieces
                # Note that (unlike reading) because of the magic of 'flush',
                # we can expect to be writing single bytes to our file
                # descriptor -- maximally inefficient!

                data = m.to_string()
                print 'Length',len(data)
                for ch in data:
                    f0.write_data(ch)        # which also flushes
                f0.send()
                r = f1.read_next_msg()
                assert r.equivalent(m)
                assert f1.next_msg() == 0

                # Since writing and sending are distinct, we can, of course,
                # decide *not* to send a message we've written
                f0.write_msg(m)
                f0.discard()
                check_IOError(errno.ENOMSG, f0.send)
                assert f1.next_msg() == 0


finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
