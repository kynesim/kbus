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
            with KSock(0, 'rw') as listener1:
                with KSock(0, 'rw') as listener2:

                    sender.kernel_module_verbose(True)

                    listener1.bind('$.Fred')
                    listener1.bind('$.Fred')

                    listener2.bind('$.Fred')
                    listener2.bind('$.Fred')
                    listener2.want_messages_once(True)

                    sender.send_msg(Message('$.Fred'))

                    assert listener1.num_messages() == 2
                    assert listener2.num_messages() == 1


finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 softtabstop=4 expandtab:
