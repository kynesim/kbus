from __future__ import with_statement

import os, time
import errno
from kbus import *

os.system('sudo insmod kbus.ko')
time.sleep(0.5)

try:
    with Interface(0,'rw') as f1:
        with Interface(0,'rw') as f2:
            with Interface(0,'rw') as f3:

                print 'f1 is',f1.bound_as()
                print 'f2 is',f2.bound_as()
                print 'f3 is',f3.bound_as()

                # f2 is listening for someone to say 'Hello'
                f2.bind('$.Hello')

                # f1 says 'Hello' to anyone listening
                m = Message('$.Hello','data')
                f1.send_msg(m)

                # We read the next message - it's a 'Hello'
                r = f2.read_next_msg()
                assert r.name == '$.Hello'
                print r

                # Two interfaces decide to listen to '$.Reponse'
                f1.bind('$.Response',True)
                f3.bind('$.Response')
                # However, f2 *cares* that f1 should receive its
                # response, and is not worried about anyone else
                # doing so
                target_id = r.from_
                print 'Hello from %d'%target_id
                m2 = Request('$.Response',data='fred',to=target_id)
                f2.send_msg(m2)

                # So, both recipients should "see" it
                r = f1.read_next_msg()
                assert r.equivalent(m2)
                r = f3.read_next_msg()
                assert r.equivalent(m2)

                # But if f1 should stop listening to the responses
                # (either because it "goes away", or because it unbinds)
                # then we want to know about this...
                f1.unbind('$.Response',True)
                try:
                    f2.send_msg(m2)
                    assert False, 'Should have failed'
                except IOError, e:
                    assert e.args[0] == errno.EADDRNOTAVAIL

                # And if someone different starts to reply, we want to
                # know about that as well
                f3.bind('$.Response',True)
                try:
                    f2.send_msg(m2)
                    assert False, 'Should have failed'
                except IOError, e:
                    assert e.args[0] == errno.EPIPE


finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
