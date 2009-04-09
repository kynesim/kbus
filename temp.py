from __future__ import with_statement

import os, time
import errno
from kbus import *
from test_kbus import check_IOError

os.system('sudo insmod kbus.ko')
time.sleep(0.5)

try:
        with KSock(0,'rw') as sender:
            with KSock(0,'rw') as listener:

                # Make the sender as unfriendly as possible
                sender.set_max_messages(1)

                listener.bind('$.Fred',True)
                req = Request('$.Fred')
                sender.send_msg(req)

                # That's one for an answer - we should have room for that.
                # So...
                sender.send_msg(req)

                m = listener.read_next_msg()
                r = Reply(m)
                listener.send_msg(r)

                # That should be OK, but there shouldn't be room to reply to
                # this next...
                m = listener.read_next_msg()
                r = Reply(m)
                check_IOError(errno.EBUSY, listener.send_msg, r)

                # Or...
                r = Reply(m,flags=Message.ALL_OR_WAIT)
                check_IOError(errno.EAGAIN, listener.send_msg, r)

                # And because we're "in" send, we can't write
                check_IOError(errno.EALREADY, listener.write_data, 'fred')

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
