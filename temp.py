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
    print '===================================================================='
    sender = KSock(0,'rw')
    listener = KSock(0,'rw')

    print 'Sender   is',sender.ksock_id()
    print 'Listener is',listener.ksock_id()

    listener.bind('$.fred.MSG')
    listener.bind('$.fred.REQ',True)
    print '/proc/kbus/bindings:'
    os.system('cat /proc/kbus/bindings')
    print '--------------------------------------------------------------------'

    print 'Unreplied to',listener.num_unreplied_to()

    r = Request('$.fred.REQ','Hello')
    print 'Sending:         ', r
    sender.send_msg(r)

    print 'Reading request: ',
    b = listener.read_next_msg()
    print b
    print 'Unreplied to',listener.num_unreplied_to()

    c = reply_to(b)
    print 'Replying with:   ',c
    listener.send_msg(c)
    print 'Unreplied to',listener.num_unreplied_to()

    #a = Announcement('$.fred.MSG','Something')
    #print 'Sending:       ', a
    #sender.send_msg(a)

    print '/proc/kbus/bindings:'
    os.system('cat /proc/kbus/bindings')
    print '/proc/kbus/stats:'
    os.system('cat /proc/kbus/stats')

    print '--------------------------------------------------------------------'
    print 'Close sender'
    sender.close()
    print 'Unreplied to',listener.num_unreplied_to()

    print '/proc/kbus/bindings:'
    os.system('cat /proc/kbus/bindings')
    print '/proc/kbus/stats:'
    os.system('cat /proc/kbus/stats')

    print '--------------------------------------------------------------------'
    print 'Close listener'
    listener.close()

    print '/proc/kbus/bindings:'
    os.system('cat /proc/kbus/bindings')
    print '/proc/kbus/stats:'
    os.system('cat /proc/kbus/stats')

    print '===================================================================='

finally:
    os.system('sudo rmmod kbus')

# vim: set tabstop=8 shiftwidth=4 expandtab:
