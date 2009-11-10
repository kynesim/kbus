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
    with KSock(0,'rw') as first:
        # Request a new device - we don't know what it will be
        next = first.new_device()
        # But we should be able to open it!
        # ...after it's had time to come into existence
        time.sleep(0.5)
        with KSock(next, 'rw') as second:
            another = second.new_device()
            assert another == (next + 1)
            time.sleep(0.5)
            with KSock(another, 'rw') as third:
                pass
finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
