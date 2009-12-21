#! /usr/bin/env python
"""Limpet - a mechanism that proxies KBUS messages to/from another Limpet.
"""

import select
import sys

from kbus import KSock

class Limpet(object):
    """A Limpet proxies KBUS messages to/from another Limpet.

    So:

    1. This Limpet communicates with one other Limpet via a (specified) socket.
    2. This Limpet communicates with a specified KBUS device, via a KSock.
    3. This Limpet has a set of messages that it will Listen to, and forward
       to the other Limpet.
    4. This Limpet has a set of messages that it will proxy as a Replier for,
       actually forwarding them to the other Limpet for passing on to another
       KBUS.
    5. This Limpet has a set of messages that it asks its paired Limpet to act
       as such a proxy Replier for.

    And probably some other things I've not yet thought of.
    """

    def __init__(self, kbus_device, limpet_socket_address):
        """A Limpet has two "ends":

        1. 'kbus_device' specifies which KBUS device it should communicate
           with, via ``ksock = KSock(kbus_device, 'rw')``.

        2. 'limpet_socket_address' is the address for the socket used to
           communicate with its paired Limpet. This should generally be a
           path name (if communication is with another Limpet on the same
           machine, via Unix domain sockets), or a ``(host, post)`` tuple (for
           communication with a Limpet on another machine, via the internet).

        Messages received from KBUS get sent to the other Limpet.

        Messages sent from the other Limpet get sent to KBUS.
        """
        self.kbus_device = kbus_device
        self.sock_address = limpet_socket_address

        self.ksock = KSock(kbus_device, 'rw')

        self.replier_for = set()
        self.listener_for = set()


    def close(self):
        """Tidy up when we're finished.
        """
        self.ksock.close()
        print 'Limpet closed'

    def __repr__(self):
        return 'Limpet(%s, %s)'%(self.kbus_device, repr(self.sock_address))

    def __str__(self):
        return 'Limpet from KSock %d via %s'%(self.kbus_device, self.sock_address)

    def bind(self, name, replier=False):
        """Bind this Limpet to Listen or Reply for the named message.

        'name' is the message name we want to bind, and if 'replier' is true
        then this Limpet will proxy as a Replier for the message name.
        """
        self.ksock.bind(name, replier)
        if replier:
            self.replier_for.add(name)
        else:
            self.listener_for.add(name)

    def unbind(self, name, replier=False):
        """Unbind this Limpet from Listening or Replying to the named message.

        'name' is the message name we want to unbind, and if 'replier' is true
        then this Limpet was proxying as a Replier for the message name.
        """
        self.ksock.unbind(name, replier)
        if replier:
            self.replier_for.remove(name)
        else:
            self.listener_for.remove(name)

    def run_forever(self):
        """Or, at least, until we're interrupted.
        """
        while 1:
            # Wait for a message written to us, with no timeout
            # (at least for the moment)
            (r, w, x) = select.select( [self.ksock], [], [])

            # Since we know that we only queued for one thing, then presumably
            # that one thing must be all we have to listen for...
            msg = self.ksock.read_next_msg()
            print 'Limpet heard',msg

    # Implement 'with'
    def __enter__(self):
        return self

    def __exit__(self, etype, value, tb):
        if tb is None:
            # No exception, so just finish normally
            self.close()
        else:
            # An exception occurred, so do any tidying up necessary
            # - well, there isn't anything special to do, really
            self.close()
            # And allow the exception to be re-raised
            return False


def main(args):
    """Run a Limpet. Use kmsg to send messages (via KBUS) to it...
    """
    with Limpet(0, '/tmp/limpet0') as l:
        print l
        l.bind('$.*')
        l.run_forever()

if __name__ == "__main__":
    args = sys.argv[1:]
    main(args)

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
