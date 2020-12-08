#! /usr/bin/env python3

import os, time
import errno
from kbus import *
from kbus.test.test_kbus import check_IOError

this_dir = os.path.split(__file__)[0]
parent_dir = os.path.split(this_dir)[0]

os.system('sudo insmod %s'%os.path.join(parent_dir, 'kbus', 'kbus.ko'))
time.sleep(0.5)

try:

        with Ksock(0, 'rw') as sender:
            with Ksock(0, 'rw') as receiver:
                with Ksock(0, 'rw') as other1:
                    with Ksock(0, 'rw') as other2:

                        sender.kernel_module_verbose(True)

                        receiver.bind('$.Question', True)
                        other1.bind('$.Question')
                        other2.bind('$.Question')

                        m = Message('$.Fred')
                        other1.send_msg(m)

                        m = Message('$.Fred')
                        other2.send_msg(m)

                        # First, a sequence that works
                        s_request = Request('$.Question', 'nice')
                        sender.send_msg(s_request)

                        r_request = receiver.read_next_msg()
                        assert r_request.equivalent(s_request)

                        r_reply = reply_to(r_request, 'nice')
                        receiver.send_msg(r_reply)

                        s_reply = sender.read_next_msg()
                        assert s_reply.equivalent(r_reply)

                        if False:
                            # Then a naughty sequence
                            s_request = Request('$.Question', 'still nice')
                            s_request_id = sender.send_msg(s_request)

                            r_request = receiver.read_next_msg()
                            assert r_request.equivalent(s_request)

                            r_reply = Reply('$.NotQuestion', data='naughty',
                                            in_reply_to=r_request.id,
                                            to=r_request.from_)
                            r_reply_id = receiver.send_msg(r_reply)

                            s_reply = sender.read_next_msg()
                            assert s_reply.equivalent(r_reply)
                            print s_request_id, s_request
                            print r_reply_id, r_reply
                            print s_reply


finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
