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
    with KSock(0,'rw') as sender:
        with KSock(0,'rw') as listener:

            def test():
                m = Message('$.fred.MSG')
                m_id = sender.send_msg(m)
                print 'Message id',m_id

                print 'Oustanding messages',listener.num_messages()

                for msg in listener:
                    print 'Message id',msg.id

                print '...'

                r = Request('$.fred.REQ')
                r_id = sender.send_msg(r)
                print 'Message id',r_id
                print 'Oustanding messages',listener.num_messages()

                for msg in listener:
                    print 'Message id',msg.id,'our request',msg.wants_us_to_reply()

            listener.bind('$.fred.MSG')
            listener.bind('$.fred.MSG')
            listener.bind('$.fred.MSG')

            only_once = listener.want_messages_once(just_ask=True)
            print 'Only once',only_once
            listener.bind('$.fred.REQ')
            listener.bind('$.fred.REQ',True)

            print '1'
            test()

            only_once = listener.want_messages_once(True)
            print 'Only once was',only_once
            only_once = listener.want_messages_once(just_ask=True)
            print 'Only once now',only_once

            print '2'
            test()

            only_once = listener.want_messages_once(False)
            print 'Only once was',only_once
            only_once = listener.want_messages_once(just_ask=True)
            print 'Only once now',only_once

            print '3'
            test()

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
