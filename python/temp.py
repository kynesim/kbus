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
        with KSock(0, 'rw') as thing:
            thing.kernel_module_verbose(True)
            # Just ask - default is off
            state = thing.report_replier_binds(True, True)
            assert not state
            # When asking, first arg doesn't matter
            state = thing.report_replier_binds(False, True)
            assert not state
            # Change it
            state = thing.report_replier_binds(True)
            assert not state
            # Just ask - now it is on
            state = thing.report_replier_binds(True, True)
            assert state
            # Change it back
            state = thing.report_replier_binds(False)
            assert state

        with KSock(0, 'rw') as thing:
            state = thing.report_replier_binds(True)
            assert not state    # It was unset

            thing.bind('$.KBUS.ReplierBindEvent')

            # There shouldn't be any outstanding messages to read
            assert thing.num_messages() == 0

            # -------- Bind as a Replier
            thing.bind('$.Fred', True)
            assert thing.num_messages() == 1
            msg = thing.read_next_msg()
            assert msg.name == '$.KBUS.ReplierBindEvent'    # of course
            # Data in the message isn't implemented yet...

            # -------- Bind as a Listener
            thing.bind('$.Jim')
            # and there shouldn't be a message
            assert thing.num_messages() == 0

            # -------- Bind as a Replier again
            thing.bind('$.Bob', True)
            assert thing.num_messages() == 1
            msg = thing.read_next_msg()
            assert msg.name == '$.KBUS.ReplierBindEvent'    # of course
            # Data in the message isn't implemented yet...

            # -------- Unbind as a Replier
            thing.unbind('$.Fred', True)
            assert thing.num_messages() == 1
            msg = thing.read_next_msg()
            assert msg.name == '$.KBUS.ReplierBindEvent'    # of course
            # Data in the message isn't implemented yet...

            # -------- Unbind as a Listener
            thing.unbind('$.Jim')
            # and there shouldn't be a message
            assert thing.num_messages() == 0

            # -------- Stop the messages
            state = thing.report_replier_binds(False)
            assert state

            # -------- Bind as a Replier
            thing.bind('$.Fred', True)
            assert thing.num_messages() == 0

            # -------- Restart the messages
            state = thing.report_replier_binds(True)
            assert not state

            # -------- Unbind as a Replier
            thing.unbind('$.Fred', True)
            assert thing.num_messages() == 1
            msg = thing.read_next_msg()
            assert msg.name == '$.KBUS.ReplierBindEvent'    # of course
            # Data in the message isn't implemented yet...

            # -------- Stop the messages (be friendly to any other tests!)
            state = thing.report_replier_binds(False)
finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
