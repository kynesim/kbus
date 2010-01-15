#! /usr/bin/env python
"""Simple Limpet testing...

Usage:  ./test.py 1 [-k <number:0>] <address>
        ./test.py 2 [-k <number:1>] <address>
        ./test.py 3 [-k <number:0>]
        ./test.py 4 [-k <number:1>]

* '1' starts the first (server) limpet, and also starts/stops KBUs
* '2' starts the second (client) limpet
* '3' starts the "listener" client
* '4' starts the "sender" client (which starts the message sending)

The first two need an <address> specifying, which must be the same - either
a <host>:<port> or a <pathname>, for communication via host/port or named
Unix domain socket respectively.

All four may also be given an explicit KSock number to connect to -
otherwise these default to 0 and 1 (i.e., two different KSocks) as
indicated.

The assumption is that each command will be run in a different terminal, and
started in numerical order.
"""

import subprocess
import sys
import time

from kbus.ksock import KSock
from kbus.messages import Message
from limpet import GiveUp, OtherLimpetGoneAway, parse_address, run_a_limpet

def help():
    print __doc__
    return

def parse_limpet_args(args, default_ksock_id):
    """Parse any arguments to our limpet.
    """
    address = None
    ksock_id = default_ksock_id

    while args:
        word = args[0]
        args = args[1:]

        if word.startswith('-'):
            if word == '-k':
                try:
                    ksock_id = int(args[0])
                except:
                    raise GiveUp('-k requires an integer argument (KSock id)')
                args = args[1:]
            else:
                raise GiveUp('Unrecognised switch "%s"'%word)
        else:
            address, family = parse_address(word)

    if address is None:
        raise GiveUp('An address (either <host>:<port> or <path>) is needed')

    return (ksock_id, address, family)

def parse_client_args(args, default_ksock_id):
    """Parse any arguments to our client.
    """
    ksock_id = default_ksock_id

    while args:
        word = args[0]
        args = args[1:]

        if word == '-k':
            try:
                ksock_id = int(args[0])
            except:
                raise GiveUp('-k requires an integer argument (KSock id)')
            args = args[1:]
        else:
            raise GiveUp('Unrecognised switch "%s"'%word)

    return ksock_id

def limpet1(args):
    """Run the first (server) limpet.

    Also start up KBUS, since we're the first runner.
    """
    print 'Limpet number 1'
    ksock_id, address, family = parse_limpet_args(args, default_ksock_id=0)
    print 'Starting KBUS'
    retval = subprocess.call('sudo insmod ../../kbus/kbus.ko kbus_num_devices=3', shell=True)
    time.sleep(0.5)
    try:
        message_names = ['$.*']
        network_id = 2
        run_a_limpet(True, address, family, ksock_id, network_id, message_names)
    finally:
        print 'Stopping KBUS'
        retval = subprocess.call('sudo rmmod kbus', shell=True)

def limpet2(args):
    """Start the second (client) limpet.
    """
    print 'Limpet number 2'
    ksock_id, address, family = parse_limpet_args(args, default_ksock_id=1)
    message_names = ['$.*']
    network_id = 1
    run_a_limpet(False, address, family, ksock_id, network_id, message_names)

def sender(args):
    """Start the first client.
    """
    ksock_id = parse_client_args(args, default_ksock_id=1)
    print '"Sender" client on KSock %d'%ksock_id

    with KSock(ksock_id, 'rw') as sender:
        msg = Message('$.Fred','1234')
        print 'Sending message',msg
        sender.send_msg(msg)

def listener(args):
    """Start the second client.
    """
    ksock_id = parse_client_args(args, default_ksock_id=0)
    print '"Listener" client on KSock %d'%ksock_id

    with KSock(ksock_id, 'rw') as sender:
        pass

actions = {'1' : limpet1,
           '2' : limpet2,
           '3' : listener,
           '4' : sender,
           '-h': help,
           '-help' : help,
           '--help' : help,
           }

def main(args):
    if args[0] not in actions.keys():
        print __doc__
        return

    actions[args[0]](args[1:])

if __name__ == "__main__":
    args = sys.argv[1:]
    try:
        main(args)
    except GiveUp as exc:
        print exc
    except OtherLimpetGoneAway as exc:
        print 'The Limpet at the other end of the connection has closed'
    except KeyboardInterrupt:
        pass

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
