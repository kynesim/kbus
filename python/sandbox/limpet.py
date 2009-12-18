#! /usr/bin/env python
"""Limpet - a mechanism that proxies KBUS messages to/from another Limpet.
"""

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
        """
        self.kbus_device = kbus_device
        self.sock_address = limpet_socket_address

        self.ksock = KSock(kbus_device, 'rw')

    def close(self):
        """Tidy up when we're finished.
        """
        self.ksock.close()

    def __repr__(self):
        return 'Limpet(%s, %s)'%(self.kbus_device, repr(self.sock_address))

    def __str__(self):
        return 'Limpet from KSock %d via %s'%(self.kbus_device, self.sock_address)

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
    """Do whatever needs doing.
    """
    with Limpet(0, '/tmp/limpet0') as l:
        print l

if __name__ == "__main__":
    args = sys.argv[1:]
    main(args)

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
