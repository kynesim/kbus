#! /usr/bin/env python

from __future__ import with_statement

import os, time
import errno
from kbus import *
from kbus.test.test_kbus import check_IOError

from kbus.messages import _struct_from_string
from kbus.messages import _MessageHeaderStruct

os.system('sudo insmod ../kbus/kbus.ko')
time.sleep(0.5)


try:

        # We need to have a bigger number than the kernel will be using
        TOO_MANY_MESSAGES = 2000

        with Ksock(0, 'rw') as first:
            first.kernel_module_verbose(True)
            first.report_replier_binds(True)

            first.set_max_messages(1)
            first.bind('$.KBUS.ReplierBindEvent')
            first.bind('$.Fred')

            with Ksock(0, 'rw') as other:
                other.set_max_messages(1)
                other.bind('$.KBUS.ReplierBindEvent')
                other.bind('$.Jim')

                with Ksock(0, 'rw') as second:
                    second_id = second.ksock_id()
                    for ii in xrange(TOO_MANY_MESSAGES):
                        # Of course, each message name needs to be unique 
                        second.bind('$.Question%d'%ii,True)
                        # Read the bind message, so it doesn't stack up
                        msg = first.read_next_msg()
                        is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
                        assert is_bind
                        assert binder_id ==second_id 
                        msg2 = other.read_next_msg()
                        assert msg == msg2

                # If this is a good test, then we won't have remembered all
                # of the messages we're "meant" to have stacked up
                num_msgs = first.num_messages()
                assert num_msgs < TOO_MANY_MESSAGES
                assert num_msgs == other.num_messages()

                # Now empty one listener's queue, but not the other
                for ii in xrange(num_msgs):
                    msg = first.read_next_msg()

                # We should be able to send to one and not the other
                first.send_msg(Message('$.Fred', flags=Message.ALL_OR_WAIT))
                check_IOError(errno.EAGAIN, first.send_msg,
                              Message('$.Jim', flags=Message.ALL_OR_WAIT))


finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
