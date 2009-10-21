from __future__ import with_statement

import os, time
import errno
from kbus import *
from test_kbus import check_IOError

from kbus import _struct_from_string
from kbus import _MessageHeaderStruct

os.system('sudo insmod kbus.ko')
time.sleep(0.5)


try:
    with KSock(0,'rw') as replier:

        replier.kernel_module_verbose(verbose=False)

        replier.bind('$.fred',True)

        with KSock(0,'rw') as sender:

            r1 = Request('$.fred','one')
            r2 = Request('$.fred','two')
            r3 = Request('$.fred','three')

            sender.write_msg(r1)
            sender.send()

            len1 = replier.next_msg()

            sender.write_msg(r2)

            m1 = replier.read_msg(len1)

            sender.send()

            x1 = reply_to(m1)
            replier.write_msg(x1)

            sender.write_msg(r3)

            replier.send()

            sender.send()

            len2 = replier.next_msg()

            m2 = replier.read_msg(len2)

            x2 = reply_to(m2)
            replier.write_msg(x2)

            replier.send()

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
