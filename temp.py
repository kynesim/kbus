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
                replier.bind('$.Fred',True)

                req = Request('$.Fred')
                req_id = sender.send_msg(req)

                rep = replier.read_next_msg()
                assert rep.id == req_id

            # Since the replier is clearly not going to give us a reply,
            # we expect KBUS to do so (and it shall be a "reply" to the
            # correct request)
            status = sender.read_next_msg()
            assert status.in_reply_to == req_id

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
