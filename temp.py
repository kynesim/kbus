from __future__ import with_statement

import os, time
import errno
from kbus import *
from test_kbus import check_IOError

os.system('sudo insmod kbus.ko')
time.sleep(0.5)

try:
        f = KSock(0,'rw')
        assert f != None
        try:
            # I don't necessarily know how much data will be "too long",
            # but we can make a good guess as to a silly sort of length
            m = Message('$.Fred',data='12345678'*1000)
            f.bind('$.Fred')
            print 'Writing'
            f.write_msg(m)
            print 'Sending'
            f.send()
        finally:
            print 'Closing'
            assert f.close() is None

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
