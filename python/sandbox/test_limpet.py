#! /usr/bin/env python
"""Tests for Limpets
"""

# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is the KBUS Lightweight Linux-kernel mediated
# message system
#
# The Initial Developer of the Original Code is Kynesim, Cambridge UK.
# Portions created by the Initial Developer are Copyright (C) 2009
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Kynesim, Cambridge UK
#   Tibs <tony.ibbs@gmail.com>
#
# ***** END LICENSE BLOCK *****

import os
import select
import socket
import subprocess
import sys
import time
import nose
from multiprocessing import Process

from kbus import KSock, Message, MessageId, Announcement, \
                 Request, Reply, Status, reply_to
from limpet import run_a_limpet, GiveUp, OtherLimpetGoneAway

NUM_DEVICES = 5
TERMINATION_MESSAGE = '$.Terminate'

KBUS_SENDER   = 1
KBUS_LISTENER = 2

TIMEOUT = 10        # 10 seconds seems like a reasonably long time...

# The Limpet processes - so we can tidy them up neatly
g_server = None
g_client = None

# Let's be good and not use os.system...
def system(command):
    """Taken from the Python reference manual. Thank you.
    """
    try:
        retcode = subprocess.call(command, shell=True)
        if retcode < 0:
            print "'%s' was terminated by signal %s"%(command, -retcode)
        else:
            print "'%s' returned %s"%(command, retcode)
        return retcode
    except OSError as e:
        print "Execution of '%s' failed: %s"%(command, e)

def our_limpet(is_server, sock_address, sock_family, kbus_device, network_id):
    """Run a standardised Limpet.
    """
    try:
        run_a_limpet(is_server, sock_address, sock_family, kbus_device,
                     network_id, termination_message=TERMINATION_MESSAGE,
                     verbosity=2)
                     #verbosity=0)
    except GiveUp as exc:
        print 'KBUS %d %s'%(kbus_device, '; '.join(exc.args))
    except OtherLimpetGoneAway as exc:
        print 'KBUS %d The Limpet at the other end of the connection has closed'%kbus_device
    except Exception as exc:
        print 'KBUS %d %s'%(kbus_device, exc)

# The "normal" KBUS test code uses a single KBUS, and tests open KSocks
# on it to send/receive messages.
#
# We, on the other hand, expect the tests to use different KBUSs for the
# two ends of a test, and provided a "blackbox" to mediate between them.
#
# This "blackbox" is actually a pair of Limpets, running as separate
# processes...

def run_limpets(sock_address, sock_family):
    """Run the Limpets for our "blackbox" KBUS communications.

    Returns the server and client
    """

    kbus_devices = network_ids  = (KBUS_SENDER, KBUS_LISTENER)

    # First, start the server Limpet
    server = Process(target=our_limpet,
                     args=(True, sock_address, sock_family, kbus_devices[0],
                           network_ids[0]))
    server.start()

    # Give it a little time to get ready for the client
    time.sleep(0.5)

    # And then *start* the client
    client = Process(target=our_limpet,
                     args=(False, sock_address, sock_family, kbus_devices[1],
                           network_ids[1]))
    client.start()

    # Again, give it time to do its connecting
    time.sleep(0.5)

    return (server, client)

def setup_module():
    # This path assumes that we are running the tests in the ``kbus/python/sandbox``
    # directory, and that the KBUS kernel module has been built in ``kbus/kbus``.
    retcode = system('sudo insmod ../../kbus/kbus.ko kbus_num_devices=%d'%NUM_DEVICES)
    try:
        assert retcode == 0
        # Via the magic of hotplugging, that should cause our device to exist
        # ...eventually
        time.sleep(1)
        # If the user has done the right magic, it should even have a predictable
        # set of permissions. We check KBUS 0 because that's the one we should
        # always find
        mode = os.stat('/dev/kbus0').st_mode
        assert mode == 020666

        global g_server, g_client
        g_server, g_client = run_limpets('fred', socket.AF_UNIX)

        # Debug output may be useful...
        for devno in range(3):
            with KSock(devno, 'rw') as friend:
                friend.kernel_module_verbose(True)

    except:
        system('sudo rmmod kbus')
        raise

def teardown_module():
    with KSock(1, 'rw') as sender:
        sender.send_msg(Message(TERMINATION_MESSAGE))

    print 'Limpet termination message sent'

    g_server.join()
    g_client.join()

    print 'Limpet server and client both finished'

    retcode = system('sudo rmmod kbus')
    assert retcode == 0
    # Via the magic of hotplugging, that should cause our device to go away
    # ...eventually
    time.sleep(1)
    assert not os.path.exists("/dev/kbus0")

class TestLimpets(object):

    def test_the_first(self):
        """An establishing test, to check we can send a single message.
        """
        with KSock(KBUS_SENDER, 'rw') as sender:
            with KSock(KBUS_LISTENER, 'rw') as listener:

                listener.bind('$.Fred')

                this = Message('$.Fred')
                this_id = sender.send_msg(this)
                print 'Sent', this

                # Wait for the message to penetrate the labyrinth
                that = listener.wait_for_msg(TIMEOUT)
                print 'Read', that

                assert this.equivalent(that)
                assert this_id.serial_num == that.id.serial_num

    def test_request_vs_message_flat(self):
        """Test repliers and Requests versus Messages (listener is replier)
        """
        with KSock(KBUS_SENDER, 'rw') as sender:
            print 'Sender',str(sender)
            with KSock(KBUS_LISTENER, 'rw') as replier:
                print 'Listener',str(replier)
                replier.bind('$.Fred.Message', False)

                # Just receiving messages should be simple
                m = Message('$.Fred.Message')
                sender.send_msg(m)
                r = replier.wait_for_msg(TIMEOUT)
                assert r.equivalent(m)
                assert not r.wants_us_to_reply()

                # Being a Replier needs a bit more synchronisation
                #
                # The replier asks to be a Replier. This sends a message to its
                # local Limpet, who tells the other Limpet (on the sender's
                # end) to register as a Replier in proxy on *its* KBUS.
                #
                # This will take a while to make its way through the system.
                # We can cheat and look for the event that occurs when the
                # "far" Limpet binds as a (proxy) Replier...
                sender.bind('$.KBUS.ReplierBindEvent')

                # So, the replier binds on *its* KBUS
                replier.bind('$.Fred.Message', True)
                # and we then wait for the bind to take effect on the sender's
                # end (i.e., for the proxying Limpet to bind)
                b = sender.wait_for_msg()
                assert b.name == '$.KBUS.ReplierBindEvent'
                # after which it should be "safe" to make our Request...

                m = Request('$.Fred.Message')
                sender.send_msg(m)
                
                # The Replier receives the Request (and should reply)
                r = replier.wait_for_msg(TIMEOUT)
                assert r.equivalent(m)
                assert r.wants_us_to_reply()

                # But if the message is an Announcement of the same name
                m = Message('$.Fred.Message')
                sender.send_msg(m)

                # The Replier will not see it
                x = replier.wait_for_msg(TIMEOUT)
                # (Hopefully our timeout is long enough to "prove" this)
                assert replier.next_msg() == 0

    def test_request_vs_message(self):
        """Test repliers and Requests versus Messages

        This is closer to the original test than the "flat" version above
        """
        with KSock(KBUS_SENDER, 'rw') as sender:
            print 'Sender',str(sender)
            with KSock(KBUS_LISTENER, 'r') as listener:
                print 'Listener',str(listener)
                listener.bind('$.Fred.Message', False)
                
                # A listener receives Messages
                m = Message('$.Fred.Message')
                sender.send_msg(m)
                r = listener.wait_for_msg(TIMEOUT)
                assert r.equivalent(m)
                assert not r.wants_us_to_reply()

                sender.bind('$.KBUS.ReplierBindEvent')

                with KSock(KBUS_LISTENER, 'rw') as replier:
                    print 'Replier',str(replier)
                    replier.bind('$.Fred.Message', True)

                    # Synchronise...
                    b = sender.wait_for_msg()
                    assert b.name == '$.KBUS.ReplierBindEvent'

                    # And a listener receives Requests (although it need not reply)
                    m = Request('$.Fred.Message')
                    sender.send_msg(m)
                    r = listener.wait_for_msg(TIMEOUT)
                    assert r.equivalent(m)
                    assert not r.wants_us_to_reply()
                    
                    # The Replier receives the Request (and should reply)
                    r = replier.wait_for_msg(TIMEOUT)
                    assert r.equivalent(m)
                    assert r.wants_us_to_reply()

                    # A replier does not receive Messages
                    m = Message('$.Fred.Message')
                    sender.send_msg(m)

                    x = listener.wait_for_msg(TIMEOUT)
                    # So we hope it *would* have had time to percolate for the
                    # replier as well
                    assert replier.next_msg() == 0

import traceback
    
if __name__ == '__main__':

    num_tests = 0

    def announce(index, name):
        if index:
            name = '%d/%d: %s'%(index,num_tests,name)
        print '%s %s %s'%('-'*10, name, '-'*(50 - len(name)))

    announce(None, 'SETUP')
    setup_module()

    t = TestLimpets()

    tests = []
    for name in dir(t):
        if name.startswith('test_'):
            tests.append(name)

    num_tests = len(tests)
    print 'Found %s tests'%num_tests

    passed = 0
    for name in tests:
        announce(passed+1, name)
        try:
            getattr(t, name)()
            passed += 1
        except Exception as exc:
            print
            print '='*60
            traceback.print_exc()
            print '='*60
            break

    announce(None, 'TEARDOWN')
    teardown_module()

    print 'Passed %d out of %d tests'%(passed, num_tests)

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
