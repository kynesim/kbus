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

        with KSock(0, 'rw') as binder:
            with KSock(0, 'rw') as listener:

                binder.kernel_module_verbose(True)   # XXX

                # Make the listener have a full queue
                assert listener.set_max_messages(1) == 1
                listener.bind('$.Filler')
                binder.send_msg(Message('$.Filler'))

                # Ask for notification
                state = binder.report_replier_binds(True)
                assert not state

                listener.bind('$.KBUS.ReplierBindEvent')

                # Try to bind as a Listener
                binder.bind('$.Jim')

                # There shouldn't be any more messages
                assert listener.num_messages() == 1

                # Try to bind as a Replier, with the Listener's queue full
                # - this should fail because we can't send the ReplierBindEvent
                check_IOError(errno.EBUSY, binder.bind, '$.Fred', True)

                # There still shouldn't be any more messages
                assert listener.num_messages() == 1

                # Ask not to get notification again
                state = binder.report_replier_binds(False)
                assert state

                # And we now *should* be able to bind as a Replier
                # (because it doesn't try to send a synthetic message)
                binder.bind('$.Fred', True)

                # And, of course, there still shouldn't be any more messages
                assert listener.num_messages() == 1

                # Then do the same sort of check for unbinding...
                state = binder.report_replier_binds(True)
                assert not state

                # Unbinding as a Listener should still just work
                binder.unbind('$.Jim')

                # Unbinding as a Replier can't send the ReplierBindEvent
                check_IOError(errno.EBUSY, binder.unbind, '$.Fred', True)

                # So, there still shouldn't be any more messages
                assert listener.num_messages() == 1
                
                # But if we stop asking for reports
                state = binder.report_replier_binds(False)
                assert state

                # We should be OK
                binder.unbind('$.Fred', True)

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
