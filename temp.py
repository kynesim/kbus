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
                # Only allow the sender a single item in its message queue
                sender.set_max_messages(1)

                listener.bind('$.Fred',True)
                req = Request('$.Fred')

                # Sending a request reserves a slot (for the eventual Reply)
                # in said message queue - so that should now be full
                msg_id = sender.send_msg(req)

                # So trying to send another request should fail
                check_IOError(errno.ENOLCK, sender.send_msg, req)

                # Sending a non-request should work, though
                a = Announcement('$.JimBob')
                sender.send_msg(a)

                # If the listener *reads* the request, it doesn't help the
                # sender
                m = listener.read_next_msg()
                check_IOError(errno.ENOLCK, sender.send_msg, req)

                # We are not allowed to send a random Reply
                # - let's construct one with an unexpected message id
                r = reply_to(m)
                x = Reply(r,in_reply_to=msg_id+100)

                check_IOError(errno.ECONNREFUSED, listener.send_msg, x)

                # But the "proper" reply should work
                listener.send_msg(r)

                # But only once...
                check_IOError(errno.ECONNREFUSED, listener.send_msg, r)

                # If the sender reads its reply:
                l = sender.read_next_msg()
                assert l.from_ == listener.ksock_id()

                # We can send a request again
                msg_id = sender.send_msg(req)

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
