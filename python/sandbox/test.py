#! /usr/bin/env python
"""Simple Limpet testing...

Usage:  ./test.py 1 [-k <number:1>] <address>
        ./test.py 2 [-k <number:2>] <address>
        ./test.py 3 [-k <number:1>]
        ./test.py 4 [-k <number:2>]

* '1' starts the first (server) limpet, and also starts/stops KBUS
* '2' starts the second (client) limpet
* '3' starts the "listener" client
* '4' starts the "sender" client (which starts the message sending)

The first two need an <address> specifying, which must be the same - either
a <host>:<port> or a <pathname>, for communication via host/port or named
Unix domain socket respectively.

All four may also be given an explicit KBUS device number to connect to -
otherwise these default to 1 and 2 (i.e., two different KBUS devices) as
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

def parse_limpet_args(args, default_kbus_device):
    """Parse any arguments to our limpet.
    """
    address = None
    kbus_device = default_kbus_device

    while args:
        word = args[0]
        args = args[1:]

        if word.startswith('-'):
            if word == '-k':
                try:
                    kbus_device = int(args[0])
                except:
                    raise GiveUp('-k requires an integer argument (KBUS device)')
                args = args[1:]
            else:
                raise GiveUp('Unrecognised switch "%s"'%word)
        else:
            address, family = parse_address(word)

    if address is None:
        raise GiveUp('An address (either <host>:<port> or <path>) is needed')

    return (kbus_device, address, family)

def parse_client_args(args, default_kbus_device):
    """Parse any arguments to our client.
    """
    kbus_device = default_kbus_device

    while args:
        word = args[0]
        args = args[1:]

        if word == '-k':
            try:
                kbus_device = int(args[0])
            except:
                raise GiveUp('-k requires an integer argument (KSock id)')
            args = args[1:]
        else:
            raise GiveUp('Unrecognised switch "%s"'%word)

    return kbus_device

def limpet1(args):
    """Run the first (server) limpet.

    Also start up KBUS, since we're the first runner.
    """
    print 'Limpet number 1'
    kbus_device, address, family = parse_limpet_args(args, default_kbus_device=1)
    print 'Starting KBUS'
    retval = subprocess.call('sudo insmod ../../kbus/kbus.ko kbus_num_devices=3', shell=True)
    time.sleep(0.5)
    try:
        message_name = '$.*'
        network_id = 1          # Limpet 1, network id 1
        run_a_limpet(True, address, family, kbus_device, network_id, message_name)
    finally:
        print 'Stopping KBUS'
        retval = subprocess.call('sudo rmmod kbus', shell=True)

def limpet2(args):
    """Start the second (client) limpet.
    """
    print 'Limpet number 2'
    kbus_device, address, family = parse_limpet_args(args, default_kbus_device=2)
    message_name = '$.*'
    network_id = 2              # Limpet 2, network id 2
    run_a_limpet(False, address, family, kbus_device, network_id, message_name)

def sender(args):
    """Start the first client.
    """
    kbus_device = parse_client_args(args, default_kbus_device=2)
    print '"Sender" client on KBUS %d'%kbus_device

    with KSock(kbus_device, 'rw') as sender:
        msg = Message('$.Fred','1234')
        print 'Sending',str(msg),
        id = sender.send_msg(msg)
        print 'with id',str(id)

def listener(args):
    """Start the second client.
    """
    kbus_device = parse_client_args(args, default_kbus_device=1)
    print '"Listener" client on KBUS %d'%kbus_device

    with KSock(kbus_device, 'rw') as sender:
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
