from __future__ import with_statement

import os, time
import errno
from kbus import *

os.system('sudo insmod kbus.ko')
time.sleep(0.5)

try:
        with Interface(0,'rw') as sender:
            with Interface(0,'r') as listener:
                listener.bind('$.Fred',guaranteed=True)
                assert listener.set_max_messages(1) == 1

                m = Message('$.Fred')
                sender.send_msg(m)
                sender.send_msg(m)

                # Because we asked for guaranteed delivery, we should
                # keep the message, even though it makes out queue too long
                assert listener.max_messages() == 1
                assert listener.num_messages() == 2

                r = listener.read_next_msg()
                assert r.equivalent(m)

                r = listener.read_next_msg()
                assert r.equivalent(m)

                assert listener.next_msg() == 0

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
