from __future__ import with_statement

import os, time
import errno
from kbus import *

os.system('sudo insmod kbus.ko')
time.sleep(0.5)

try:
        with Interface(0,'rw') as f1:
            with Interface(0,'rw') as f2:
                f2.bind('$.Message')
                f2.bind('$.Request',True)

                f2.bind('$.Urgent.Message')
                f2.bind('$.Urgent.Request',True)


                m1 = Message('$.Message')
                m2 = Message('$.Message')

                r1 = Request('$.Request')
                r2 = Request('$.Request')

                f1.send_msg(m1)
                m1_id = f1.last_msg_id()

                f1.send_msg(r1)
                r1_id = f1.last_msg_id()

                f1.send_msg(m2)
                m2_id = f1.last_msg_id()

                f1.send_msg(r2)
                r2_id = f1.last_msg_id()

                # Timeline should be: m1, r1, m2, r2

                mu1 = Message('$.Urgent.Message')
                mu1.set_urgent()
                f1.send_msg(mu1)
                mu1_id = f1.last_msg_id()

                ru1 = Request('$.Urgent.Request')
                ru1.set_urgent()
                f1.send_msg(ru1)
                ru1_id = f1.last_msg_id()

                # Timeline should be: ru1, mu1, m1, r1, m2, r2

                a = f2.read_next_msg()          # ru1
                assert a.equivalent(ru1)
                assert a.id == ru1_id
                assert a.is_urgent()
                assert a.wants_us_to_reply()

                a = f2.read_next_msg()          # mu1
                assert a.equivalent(mu1)
                assert a.id == mu1_id
                assert a.is_urgent()
                assert not a.wants_us_to_reply()

                a = f2.read_next_msg()          # m1
                assert a.equivalent(m1)
                assert a.id == m1_id
                assert not a.is_urgent()
                assert not a.wants_us_to_reply()

                a = f2.read_next_msg()          # r1
                assert a.equivalent(r1)
                assert a.id == r1_id
                assert not a.is_urgent()
                assert a.wants_us_to_reply()

                a = f2.read_next_msg()          # m2
                assert a.equivalent(m2)
                assert a.id == m2_id
                assert not a.is_urgent()
                assert not a.wants_us_to_reply()

                a = f2.read_next_msg()          # r2
                assert a.equivalent(r2)
                assert a.id == r2_id
                assert not a.is_urgent()
                assert a.wants_us_to_reply()

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
