from __future__ import with_statement

import os, time
import errno
from kbus import *

os.system('sudo insmod kbus.ko')
time.sleep(0.5)

try:
    with Interface(0,'rw') as f1:
        f1_id = f1.bound_as()
        f2_id = 0

        m = Request('$.Fred','data')

        with Interface(0,'rw') as f2:
            f2_id = f2.bound_as()
            f2.bind('$.Fred',replier=True)
            f2.bind('$.Fred',replier=False)

            f1.send_msg(m)
            m_id = f1.last_msg_id()

        # And f2 closes ("releases" internally)
        # - the Message should just be lost, but we should be told about
        #   the Request
        e = f1.read_next_msg()

        print 'f1 is',f1_id
        print 'f2 is',f2_id
        print m
        print e

        assert e.to    == f1_id
        assert e.from_ == f2_id
        assert e.in_reply_to == m_id
        assert e.name == '$.KBUS.Replier.GoneAway'
        assert e.is_synthetic()
        assert len(e.data) == 0

        assert f1.next_msg() == 0

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
