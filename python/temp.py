#! /usr/bin/env python

from __future__ import with_statement

import os, time
import errno
from kbus import *
from kbus.test.test_kbus import check_IOError

os.system('sudo insmod ../kbus/kbus.ko')
time.sleep(0.5)


try:

        with Ksock(0, 'rw') as sender:
            with Ksock(0, 'rw') as listener:

                listener.bind('$.Fred')

                data = 'x' * (64*1024 + 27)

                msg = Announcement('$.Fred', data=data)

                orig_max = None
                try:
                    listener.kernel_module_verbose(True)
                    # Make sure we're allowed to send messages of that length
                    orig_max = listener.max_message_size()
                    listener.set_max_message_size(msg.total_length())
                    sender.send_msg(msg)
                finally:
                    # Don't forget to set the world back again
                    if orig_max is not None:
                        listener.set_max_message_size(orig_max)

                ann = listener.read_next_msg()
                assert msg.equivalent(ann)


finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
