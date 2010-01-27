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

        TOO_MANY_MESSAGES = 2000        # Really?

        with KSock(0, 'rw') as first:
            first.kernel_module_verbose(True)
            first.report_replier_binds(True)
            first.set_max_messages(1)
            first.bind('$.KBUS.ReplierBindEvent')

            with KSock(0, 'rw') as second:
                second_id = second.ksock_id()
                for ii in xrange(TOO_MANY_MESSAGES):
                    # Of course, each message name needs to be unique 
                    second.bind('$.Question%d'%ii,True)
                    # Read the bind message, so it doesn't stack up
                    msg = first.read_next_msg()
                    is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
                    assert is_bind
                    assert binder_id ==second_id 

            # If this is a good test, then we won't have remembered all
            # of the messages we're "meant" to have stacked up
            num_msgs = first.num_messages()
            assert num_msgs < TOO_MANY_MESSAGES

            # All but the last message should be unbind events
            for ii in xrange(num_msgs-1):
                msg = first.read_next_msg()
                assert msg.name == '$.KBUS.ReplierBindEvent'
                is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
                assert not is_bind
                assert binder_id == second_id

            # The last message should be different
            assert first.num_messages() == 1
            msg = first.read_next_msg()
            assert msg.name == '$.KBUS.UnbindEventsLost'
            assert msg.data == None

            assert first.num_messages() == 0


finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
