from __future__ import with_statement

import os, time
import errno
from kbus import *
from test_kbus import check_IOError

from kbus import _struct_from_string
from kbus import _MessageHeaderStruct

os.system('sudo insmod kbus.ko')
time.sleep(0.5)


try:
    with KSock(0,'rw') as sender:
        with KSock(0,'rw') as listener:

            #data = 'x'*4096*20

            data = 'x'*1024*64

            print 'Making message'

            m = Request('$.Fred', data=data)
            #print m

            listener.bind('$.Fred', replier=True)
            listener.bind('$.Fred', replier=False)

            print 'Sending message'

            sender.send_msg(m)

            print 'Reading message (replier)'

            # Once as a replier
            r = listener.read_next_msg()
            #print r
            assert r.equivalent(m)
            assert r.wants_us_to_reply()

            print 'Reading message (listener)'

            # Once as a listener
            r = listener.read_next_msg()
            #print r
            assert r.equivalent(m)
            assert not r.wants_us_to_reply()

            assert sender.next_msg() == 0

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
