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
                print m

                # And f2 unbinds as a replier
                #   the Request should be lost, and f1 should get told
                # - the Message should still be "in transit"
                f2.unbind('$.Fred',replier=True)

                # Read the "gone away" synthetic message
                e = f1.read_next_msg()
                print e

                assert e.to    == f1_id
                assert e.from_ == f2_id
                assert e.in_reply_to == m_id
                assert e.name == '$.KBUS.Replier.Unbound'
                assert e.is_synthetic()
                assert len(e.data) == 0

                assert f1.next_msg() == 0

                # read the Message
                r = f2.read_next_msg()
                assert r.equivalent(m)
                assert not r.wants_us_to_reply()

                assert f2.next_msg() == 0

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
