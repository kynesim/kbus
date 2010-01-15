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

        with KSock(0, 'rw') as sender:
            with KSock(0, 'rw') as listener:
                listener.bind('$.Fred')
                data = '1\x0034'
                msg = Message('$.Fred', data=data)
                print msg
                sender.send_msg(msg)
                got = listener.read_next_msg()
                print got
                assert got.data == data

                print hexify(msg.to_string())
                print hexify(got.to_string())


        with KSock(0, 'rw') as binder:
            with KSock(0, 'rw') as listener:

                # Ask for notification
                state = binder.report_replier_binds(True)
                assert not state

                listener.bind('$.KBUS.ReplierBindEvent')

                binder.bind('$.Fred', True)

                msg = listener.read_next_msg()

                print 'Event message %s'%msg
                print 'Data %s'%hexify(msg.data)

                is_bind, binder_id, name = split_replier_bind_event_data(msg.data)

                assert is_bind == 1
                assert binder_id == binder.ksock_id()
                assert name == '$.Fred'


finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
