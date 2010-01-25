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

        with KSock(0, 'rw') as first:
            first.kernel_module_verbose(True)
            first.report_replier_binds(True)
            first.set_max_messages(1)
            first.bind('$.KBUS.ReplierBindEvent')

            second_id = 0
            with KSock(0, 'rw') as second:
                second_id = second.ksock_id()
                second.bind('$.Question',True)
                assert first.num_messages() == 1  # and thus our message queue is full

            assert first.num_messages() == 1  # and our message queue is still full
            msg = first.read_next_msg()
            assert msg.name == '$.KBUS.ReplierBindEvent'
            is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
            assert is_bind
            assert binder_id == second_id

            assert first.num_messages() == 1  # the deferred unbind message
            msg = first.read_next_msg()
            assert msg.name == '$.KBUS.ReplierBindEvent'
            is_bind, binder_id, name = split_replier_bind_event_data(msg.data)
            assert not is_bind
            assert binder_id == second_id

            assert first.num_messages() == 0


finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
