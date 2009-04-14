from __future__ import with_statement

import os, time
import errno
from kbus import *
from test_kbus import check_IOError

os.system('sudo insmod kbus.ko')
time.sleep(0.5)

try:
        msg = Message('$.Fred',data='abcd',from_=27,to=99,id=MessageId(0,132),flags=Message.WANT_A_REPLY)
        print msg
        reply = reply_to(msg)
        print reply
        print
        print msg.extract()
        print Message(msg.array.tostring())
        rep3 = reply_to(msg.array.tostring())
        assert rep3 == reply
finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
