from __future__ import with_statement

import os, time
import errno
from kbus import *
from test_kbus import check_IOError

os.system('sudo insmod kbus.ko')
time.sleep(0.5)

try:
        with KSock(0,'rw') as sender:
            with KSock(0,'rw') as replier:
                sender.set_max_messages(1050)
                replier.set_max_messages(1050)
                replier.bind('$.Fred',True)
                req = Request('$.Fred','1234')
                for ii in range(1000):
                    sender.send_msg(req)

                for r in replier:
                    m = reply_to(r)
                    replier.send_msg(m)

                count = 0
                for m in sender:
                    count += 1

                assert count == 1000

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
