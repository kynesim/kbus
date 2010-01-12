#! /usr/bin/env python
"""Limpet - a mechanism that proxies KBUS messages to/from another Limpet.

Usage:

    limpet.py  <things>

This runs a client or server limpet, talking to a server or client limpet
(respectively).

The <things> specify what the Limpet is to do. The order of <things> on the
command line is not significant, but if a later <thing> contradicts an earlier
<thing>, the later <thing> wins.

<thing> may be:

    <host>:<port>   Communicate via the specified host and port.
    <path>          Communicate via the named Unix domain socket.

        One or the other communication mechanism must be specified.

    -c, -client     This is a client Limpet.
    -s, -server     This is a server Limpet.

        Either client or server must be specified.

    -id <number>    Messages sent by this Limpet (to the other Limpet) will
                    have network ID <number>. This defaults to 1 for a client
                    and 2 for a server.

    -k <number>, -ksock <number>
                    Connect to the given KSock. The default is to connect to
                    KSock 0.

    -m <name>, -message <name>
                    Proxy any messages with this name to the other Limpet.
"""

import ctypes
import os
import select
import socket
import sys

from kbus import KSock, Message
from kbus.messages import _MessageHeaderStruct, _struct_from_string

MSG_HEADER_LEN = ctypes.sizeof(_MessageHeaderStruct)

class GiveUp(Exception):
    pass

class OtherLimpetGoneAway(Exception):
    """The other end has closed its end of the socket.
    """
    pass

class BadMessage(Exception):
    """We have read a badly formatted KBUS message.
    """
    pass

def msgstr(msg):
    """Return a short string for what we want to know of a message.
    """
    if msg.wants_us_to_reply():
        what = 'Request (to us)'
    elif msg.in_reply_to:
        what = 'Reply (to ksock %d)'%msg.in_reply_to
    else:
        what = 'Message'
    return '%s "%s" [%d,%d]: "%s", from %s, to %s'%(what, msg.name, msg.id.network_id,
            msg.id.serial_num, msg.data, msg.from_, msg.to)

def padded_name_len(name_len):
    """Calculate the length of a message name, in bytes, after padding.

    Matches the definition in the kernel module's header file
    """
    return 4 * ((name_len + 1 + 3) // 4)

def padded_data_len(data_len):
    """Calculate the length of message data, in bytes, after padding.

    Matches the definition in the kernel module's header file
    """
    return 4 * ((data_len + 3) // 4)

def entire_message_len(name_len, data_len):
    """Calculate the "entire" message length, from the name and data lengths.

    All lengths are in bytes.

    Matches the definition in the kernel module's header file
    """
    return MSG_HEADER_LEN + padded_name_len(name_len) + \
                          padded_data_len(data_len) + 4

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

    def __init__(self, kbus_device, network_id, limpet_socket_address, is_server, socket_family):
        """A Limpet has two "ends":

        1. 'kbus_device' specifies which KBUS device it should communicate
           with, via ``ksock = KSock(kbus_device, 'rw')``.

        2. 'limpet_socket_address' is the address for the socket used to
           communicate with its paired Limpet. This should generally be a
           path name (if communication is with another Limpet on the same
           machine, via Unix domain sockets), or a ``(host, port)`` tuple (for
           communication with a Limpet on another machine, via the internet).

        Messages received from KBUS get sent to the other Limpet.

        Messages sent from the other Limpet get sent to KBUS.
        """
        self.kbus_device = kbus_device
        self.sock_address = limpet_socket_address
        self.sock_family = socket_family
        self.is_server = is_server
        self.network_id = network_id

        if socket_family not in (socket.AF_UNIX, socket.AF_INET):
            raise GiveUp('Socket family is %d, must be AF_UNIX (%s) or AF_INET (%d)'%(socket_family,
                socket.AF_UNIX, socket.AF_INET))

        self.sock = None
        self.listener = None
        self.ksock = KSock(kbus_device, 'rw')

        self.replier_for = set()
        self.listener_for = set()

        if is_server:
            self.sock = self.setup_as_server(limpet_socket_address, socket_family)
        else:
            self.sock = self.setup_as_client(limpet_socket_address, socket_family)

    def setup_as_server(self, address, family):
        """Set ourselves up as a server Limpet.

        We start listening, until we get someone connecting to us.
        """
        print 'Listening on', address
        listener = self.listener = socket.socket(family, socket.SOCK_STREAM)
        # Try to allow address reuse as soon as possible after we've finished
        # with it
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(address)
        try:
            listener.listen(1)
            connection, address = self.listener.accept()
            print 'Connection accepted from (%s, %s)'%(connection, address)

            return connection
        except:
            self.close()
            raise

    def setup_as_client(self, address, family):
        """Set ourselves up as a client Limpet.
        """
        if family == socket.AF_INET:
            sockname = '%s:%s'%address
        else:
            sockname = address

        try:
            sock = socket.socket(family, socket.SOCK_STREAM)
            sock.connect(address)
            print 'Connected to "%s" as client'%sockname
            return sock
        except Exception as exc:
            raise GiveUp('Unable to connect to "%s" as client: %s'%(sockname, exc))


    def close(self):
        """Tidy up when we're finished.
        """
        if self.ksock:
            self.ksock.close()
            self.ksock = None

        if self.sock:
            print 'Closing socket'
            if self.is_server:
                print '...for server'
                self.sock.close()
            else:
                print '...for client'
                self.sock.shutdown(socket.SHUT_RDWR)
                self.sock.close()
            self.sock = None

        if self.listener:
            print 'Closing listener socket'
            self.listener.close()
            self.listener = None
            if self.sock_family == socket.AF_UNIX:
                print '...removing socket file'
                self.remove_socket_file(self.sock_address)

        print 'Limpet closed'

    def remove_socket_file(self, name):
        # Assuming this is an address representing a file in the filesystem,
        # delete it so we can use it again...
        try:
            os.remove(name)
        except Exception as err:
            raise GiveUp('Unable to delete socket file "%s": %s'%(name, err))

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

    def read_message_from_socket(self):
        """Read a message from the other Limpet.

        Returns the corresponding Message instance.
        """

        # All KBUS messages start with the start guard:
        print 'Waiting for a message start guard: length 4'
        start_guard = self.sock.recv(4, socket.MSG_WAITALL)
        if start_guard == '':
          raise OtherLimpetGoneAway()
        ##elif start_guard == 'QUIT': # Sort-of out of bound data
        ##  raise OutOfBoundQuit('Client asked us to QUIT')
        elif start_guard != 'kbus': # This is perhaps a bit naughty, relying on byte order
          raise BadMessage('Data read starts with "%s", not "kbus"'%start_guard)

        # So we start with the message header - this is always the same length
        print 'Read the rest of message header: length',MSG_HEADER_LEN-4
        rest_of_header_data = self.sock.recv(MSG_HEADER_LEN-4, socket.MSG_WAITALL)

        header_data = start_guard + rest_of_header_data

        header = _struct_from_string(_MessageHeaderStruct, header_data)

        overall_length = entire_message_len(header.name_len, header.data_len)

        print 'Message header: header length %d, total length %d'%(MSG_HEADER_LEN,
          overall_length)

        print 'Reading rest of message: length', overall_length - MSG_HEADER_LEN
        rest_of_message = self.sock.recv(overall_length - MSG_HEADER_LEN, socket.MSG_WAITALL)

        return Message(header_data + rest_of_message)

    def write_message_to_socket(self, msg):
        """Write a Message to the other Limpet.

        Adjust it to reflect our network id.

        Caveats:

        1. We change the network id of the message given to us, which is
           a non-obvious SIDE EFFECT - we could instead copy the message
           and change the copy (or amend the "string" representation of
           the message before sending it!)

        2. We do not change the network id if it was already non-zero
           (i.e., came to us from another network). Is this the right
           thing to do?
        """
        if msg.id.network_id == 0:
            msg.id.network_id = self.network_id
        data = msg.to_string()
        self.sock.sendall(data)

    def run_forever(self):
        """Or, at least, until we're interrupted.
        """
        while 1:
            # Wait for a message written to us, with no timeout
            # (at least for the moment)
            (r, w, x) = select.select( [self.ksock, self.sock], [], [])

            if self.ksock in r:
                msg = self.ksock.read_next_msg()
                print 'KBUS->Us  ',msgstr(msg)
                self.write_message_to_socket(msg)
                print 'Us->Limpet',msgstr(msg)  # i.e., with amended network id

            if self.sock in r:
                msg = self.read_message_from_socket()
                print 'Limpet->Us',msgstr(msg)
                self.ksock.send_msg(msg)
                print 'Us->KBUS  ',msgstr(msg)

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


def run_a_limpet(is_server, address, family, ksock_id, network_id, message_names):
    """Run a Limpet. Use kmsg to send messages (via KBUS) to it...
    """
    with Limpet(ksock_id, network_id, address, is_server, family) as l:
        print l
        for message_name in message_names:
            try:
                l.bind(message_name)
                print 'Bound to message name "%s"'%message_name
            except IOError as exc:
                raise GiveUp('Unable to bind to message name "%s": %s'%(message_name, exc))

        print "Use 'kmsg -bus %d send <message_name> s <data>'"%ksock_id
        print " or 'kmsg -bus %d call <message_name> s <data>' to send messages."%ksock_id
        l.run_forever()

def main(args):
    """Work out what we've been asked to do and do it.
    """
    is_server = None            # no default
    address = None              # ditto
    ksock_id = 0
    network_id = None
    message_names = []

    if not args:
        print __doc__
        return

    while args:
        word = args[0]
        args = args[1:]

        if word.startswith('-'):
            if word in ('-c', '-client'):
                is_server = False
            elif word in ('-s', '-server'):
                is_server = True
            elif word in ('-id'):
                try:
                    network_id = int(args[0])
                except:
                    raise GiveUp('-id requires an integer argument (network id)')
                args = args[1:]
            elif word in ('-m', '-message'):
                try:
                    message_names.append(args[0])
                except:
                    raise GiveUp('-message requires an argument (message name)')
                args = args[1:]
            elif word in ('-k', '-ksock'):
                try:
                    ksock_id = int(args[0])
                except:
                    raise GiveUp('-ksock requires an integer argument (KSock id)')
                args = args[1:]
            else:
                print __doc__
                return
        else:
            # Deliberately allow multiple "address" values, using the last
            if ':' in word:
                try:
                    host, port = word.split(':')
                    port = int(port)
                    address = host, port
                    family = socket.AF_INET
                except Exception as exc:
                    raise GiveUp('Unable to interpret "%s" as <host>:<port>: %s'%(word, exc))
            else:
                # Assume it's a valid pathname (!)
                address = word
                family = socket.AF_UNIX

    if is_server is None:
        raise GiveUp('Either -client or -server must be specified')

    if address is None:
        raise GiveUp('An address (either <host>:<port> or <path>) is needed')

    if network_id is None:
        network_id = 2 if is_server else 1

    print 'Limpet: %s via %s for KSock %d, using network id %d'%('server' if is_server else 'client',
            address, ksock_id, network_id)

    # And then do whatever we've been asked to do...
    run_a_limpet(is_server, address, family, ksock_id, network_id, message_names)

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
