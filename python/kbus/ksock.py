"""The definition of a KBUS Ksock.

On Ubuntu, if I want ordinary users (in the admin group) to be able to
read/write '/dev/kbus0' then I need to have a file
'/etc/udev/rules.d/45-kbus.rules' which contains::

    KERNEL=="kbus[0-9]*",  MODE="0666", GROUP="admin"

Other operating systems will have other mechanisms, and on an embedded system
it is likely enough not to do this, as the "user" will be root.
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

from __future__ import with_statement
import fcntl
import ctypes
import array
import select

from kbus.messages import MessageId, Message

# Kernel definitions for ioctl commands
# Following closely from #include <asm[-generic]/ioctl.h>
# (and with some thanks to http://wiki.maemo.org/Programming_FM_radio)
_IOC_NRBITS   = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14
_IOC_DIRBITS  = 2

_IOC_NRSHIFT   = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT  = _IOC_SIZESHIFT + _IOC_SIZEBITS

_IOC_NONE  = 0
_IOC_WRITE = 1
_IOC_READ  = 2

# Mustn't use "type" as an argument, since Python already has it...
def _IOC(d, t, nr, size):
    return ((d << _IOC_DIRSHIFT) | (ord(t) << _IOC_TYPESHIFT) | 
            (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT))
def _IO(t, nr):
    return _IOC(_IOC_NONE, t, nr, 0)
def _IOW(t, nr, size):                          # write to device
    return _IOC(_IOC_WRITE, t, nr, size)
def _IOR(t, nr, size):                          # read from device
    return _IOC(_IOC_READ, t, nr, size)
def _IOWR(t, nr, size):                         # read and write
    return _IOC(_IOC_READ | _IOC_WRITE, t, nr, size)


class BindStruct(ctypes.Structure):
    """The datastucture we need to describe an IOC_BIND argument
    """
    _fields_ = [('is_replier', ctypes.c_uint32),
                ('len',        ctypes.c_uint32),
                ('name',       ctypes.c_char_p)]

class ReplierStruct(ctypes.Structure):
    """The datastucture we need to describe an IOC_REPLIER argument
    """
    _fields_ = [('return_id', ctypes.c_uint32),
                ('len',       ctypes.c_uint32),
                ('name',      ctypes.c_char_p)]

class SendResultStruct(ctypes.Structure):
    """The datastucture we need to describe an IOC_SEND argument/return
    """
    _fields_ = [('retval',  ctypes.c_int32),
                ('msg_id',  MessageId)]


class Ksock(object):
    """A wrapper around a KBUS device, for purposes of message sending.

    'which' is which KBUS device to open -- so if 'which' is 3, we open
    /dev/kbus3.

    'mode' should be 'r' or 'rw' -- i.e., whether to open the device for read or
    write (opening for write also allows reading, of course).

    I'm not really very keen on the name Ksock, but it's better than the
    original "File", which I think was actively misleading.
    """

    IOC_MAGIC = 'k'
    IOC_RESET       = _IO(IOC_MAGIC,    1)
    IOC_BIND        = _IOW(IOC_MAGIC,   2, ctypes.sizeof(ctypes.c_char_p))
    IOC_UNBIND      = _IOW(IOC_MAGIC,   3, ctypes.sizeof(ctypes.c_char_p))
    IOC_KSOCKID     = _IOR(IOC_MAGIC,   4, ctypes.sizeof(ctypes.c_char_p))
    IOC_REPLIER     = _IOWR(IOC_MAGIC,  5, ctypes.sizeof(ctypes.c_char_p))
    IOC_NEXTMSG     = _IOR(IOC_MAGIC,   6, ctypes.sizeof(ctypes.c_char_p))
    IOC_LENLEFT     = _IOR(IOC_MAGIC,   7, ctypes.sizeof(ctypes.c_char_p))
    IOC_SEND        = _IOR(IOC_MAGIC,   8, ctypes.sizeof(ctypes.c_char_p))
    IOC_DISCARD     = _IO(IOC_MAGIC,    9)
    IOC_LASTSENT    = _IOR(IOC_MAGIC,  10, ctypes.sizeof(ctypes.c_char_p))
    IOC_MAXMSGS     = _IOWR(IOC_MAGIC, 11, ctypes.sizeof(ctypes.c_char_p))
    IOC_NUMMSGS     = _IOR(IOC_MAGIC,  12, ctypes.sizeof(ctypes.c_char_p))
    IOC_UNREPLIEDTO = _IOR(IOC_MAGIC,  13, ctypes.sizeof(ctypes.c_char_p))
    IOC_MSGONLYONCE = _IOWR(IOC_MAGIC, 14, ctypes.sizeof(ctypes.c_char_p))
    IOC_VERBOSE     = _IOWR(IOC_MAGIC, 15, ctypes.sizeof(ctypes.c_char_p))
    IOC_NEWDEVICE   = _IOR(IOC_MAGIC,  16, ctypes.sizeof(ctypes.c_char_p))
    IOC_REPORTREPLIERBINDS = _IOWR(IOC_MAGIC, 17, ctypes.sizeof(ctypes.c_char_p))

    def __init__(self, which=0, mode='r'):
        if mode not in ('r', 'rw'):
            raise ValueError("Ksock mode should be 'r' or 'rw', not '%s'"%mode)
        self.which = which
        self.name = '/dev/kbus%d'%which
        if mode == 'r':
            self.mode = 'read'
        else:
            mode = 'w+'
            self.mode = 'read/write'
        # Although Unix doesn't mind whether a file is opened with a 'b'
        # for binary, it is possible that some version of Python may
        self.fd = open(self.name, mode+'b')

    def __str__(self):
        if self.fd:
            return 'Ksock device %d, id %d, mode %s'%(self.which,
                    self.ksock_id(), self.mode)
        else:
            return 'Ksock device %d (closed)'%(self.which)

    def __repr__(self):
        if self.fd:
            return '<Ksock %s open for %s>'%(self.name, self.mode)
        else:
            return '<Ksock %s closed>'%(self.name)

    def close(self):
        ret = self.fd.close()
        self.fd = None
        self.mode = None
        return ret

    def bind(self, name, replier=False):
        """Bind the given name to the file descriptor.

        If 'replier', then we are binding as the only fd that can reply to this
        message name.
        """
        arg = BindStruct(replier, len(name), name)
        fcntl.ioctl(self.fd, Ksock.IOC_BIND, arg)

    def unbind(self, name, replier=False):
        """Unbind the given name from the file descriptor.

        The arguments need to match the binding that we want to unbind.
        """
        arg = BindStruct(replier, len(name), name)
        fcntl.ioctl(self.fd, Ksock.IOC_UNBIND, arg)

    def ksock_id(self):
        """Return the internal 'Ksock id' for this file descriptor.
        """
        # Instead of using a ctypes.Structure, we can retrieve homogenious
        # arrays of data using, well, arrays. This one is a bit minimalist.
        id = array.array('L', [0])
        fcntl.ioctl(self.fd, Ksock.IOC_KSOCKID, id, True)
        return id[0]

    def next_msg(self):
        """Say we want to start reading the next message.

        Returns the length of said message, or 0 if there is no next message.
        """
        id = array.array('L', [0])
        fcntl.ioctl(self.fd, Ksock.IOC_NEXTMSG, id, True)
        return id[0]

    def len_left(self):
        """Return how many bytes of the current message are still to be read.

        Returns 0 if there is no current message (i.e., 'next_msg()' has not
        been called), or if there are no bytes left.
        """
        id = array.array('L', [0])
        fcntl.ioctl(self.fd, Ksock.IOC_LENLEFT, id, True)
        return id[0]

    def send(self):
        """Send the last written message.

        Indicates that we have finished writing a message, and it should
        be sent.

        Returns the message id of the send message.

        Raises IOError with errno ENOMSG if there was no message to send.
        """
        arg = array.array('L', [0, 0])
        fcntl.ioctl(self.fd, Ksock.IOC_SEND, arg);
        return MessageId(arg[0], arg[1])

    def discard(self):
        """Discard the message being written.

        Indicates that we have should throw away the message we've been
        writing. Has no effect if there is no current message being
        written (for instance, because 'send' has already been called).
        be sent.
        """
        fcntl.ioctl(self.fd, Ksock.IOC_DISCARD, 0);

    def last_msg_id(self):
        """Return the id of the last message written on this file descriptor.

        Returns 0 before any messages have been sent.
        """
        id = array.array('L', [0, 0])
        fcntl.ioctl(self.fd, Ksock.IOC_LASTSENT, id, True)
        return MessageId(id[0], id[1])

    def find_replier(self, name):
        """Find the id of the replier (if any) for this message.

        Returns None if there was no replier, otherwise the replier's id.
        """
        arg = ReplierStruct(0, len(name), name)
        retval = fcntl.ioctl(self.fd, Ksock.IOC_REPLIER, arg);
        if retval:
            return arg.return_id
        else:
            return None

    def max_messages(self):
        """Return the number of messages that can be queued on this Ksock.
        """
        id = array.array('L', [0])
        fcntl.ioctl(self.fd, Ksock.IOC_MAXMSGS, id, True)
        return id[0]

    def set_max_messages(self, count):
        """Set the number of messages that can be queued on this Ksock.

        A 'count' of 0 does not actually change the value - this may thus be
        used to query the Ksock for the current value of the maximum.
        However, the "more Pythonic" 'max_messages()' method is provided for
        use when such a query is wanted, which is just syntactic sugar around
        such a call.

        Returns the number of messages that are allowed to be queued on this
        Ksock.
        """
        id = array.array('L', [count])
        fcntl.ioctl(self.fd, Ksock.IOC_MAXMSGS, id, True)
        return id[0]

    def num_messages(self):
        """Return the number of messages that are queued on this Ksock.
        """
        id = array.array('L', [0])
        fcntl.ioctl(self.fd, Ksock.IOC_NUMMSGS, id, True)
        return id[0]

    def num_unreplied_to(self):
        """Return the number of replies we still have outstanding.

        That is, the number of Requests that we have read, which had the
        WANT_YOU_TO_REPLY flag set, but for which we have not yet sent a
        Reply.
        """
        id = array.array('L', [0])
        fcntl.ioctl(self.fd, Ksock.IOC_UNREPLIEDTO, id, True)
        return id[0]

    def want_messages_once(self, only_once=False, just_ask=False):
        """Determine whether multiply-bound messages are only received once.

        Determine whether we should receive a particular message once, even if
        it we are both a Replier and Listener for the message, or if it we are
        registered more than once as a Listener for the message name.

        Note that in the case of a Request that we should reply to, we will
        always get the Request, and it will be the Listener's version of the
        message that will be "dropped".

        The default is False, i.e., to receive each message as many times as we
        are bound to its name.

        * if 'only_once' is true then we want to receive each message once only.
        * if 'just_ask' is true, then we just want to find out the current state
          of the flag, and 'only_once' will be ignored.

        Returns the previous value of the flag (i.e., what it used to be set to).
        Which, if 'just_ask' is true, will also be the current state.

        Beware that setting this flag affects how messages are added to the
        Ksock's message queue *as soon as it is set* - so changing it and then
        changing it back "at once" is not (necessarily) a null operation.
        """
        if just_ask:
            val = 0xFFFFFFFF
        elif only_once:
            val = 1
        else:
            val = 0
        id = array.array('L', [val])
        fcntl.ioctl(self.fd, Ksock.IOC_MSGONLYONCE, id, True)
        return id[0]

    def kernel_module_verbose(self, verbose=True, just_ask=False):
        """Determine whether the kernel module should output verbose messages.

        Determine whether the kernel module should output verbose messages for
        this device (this Ksock). This will only have any effect if the kernel
        module was built with VERBOSE_DEBUG defined.

        The default is False, i.e., not to output verbose messages (as this
        clutters up the kernel log).

        * if 'verbose' is true then we want verbose messages.
        * if 'just_ask' is true, then we just want to find out the current state
          of the flag, and 'verbose' will be ignored.

        Returns the previous value of the flag (i.e., what it used to be set to).
        Which, if 'just_ask' is true, will also be the current state.

        Beware that setting this flag affects the Ksock as a whole, so it is
        possible for several programs to open a Ksock and "disagree" about how
        this flag should be set.
        """
        if just_ask:
            val = 0xFFFFFFFF
        elif verbose:
            val = 1
        else:
            val = 0
        id = array.array('L', [val])
        fcntl.ioctl(self.fd, Ksock.IOC_VERBOSE, id, True)
        return id[0]

    def new_device(self):
        """Request that KBUS set up a new device (/dev/kbus<n>).

        Note that it can take a little while for the hotplugging mechanisms
        to set the new device up for user access.

        Returns the new device number (<n>).
        """
        id = array.array('L', [0])
        fcntl.ioctl(self.fd, Ksock.IOC_NEWDEVICE, id, True)
        return id[0]

    def report_replier_binds(self, report_events=True, just_ask=False):
        """Determine whether the kernel module should report Replier bind/unbind events.

        Determine whether the kernel module should output a "synthetic" message
        to announce each Replier bind/unbind event.

        When the flag is set, then each time a Replier binds or unbinds to a
        message (i.e., when ``ksock.bind(name,True)`` or
        ``ksock.unbind(name,True`` is called), a message will automatically
        be generated and sent.

        The message generated is called '$.KBUS.ReplierBindEvent', and it has
        data:

          * a 32-bit value, 1 if this is a bind, 0 if it is an unbind
          * a 32-bit value, the Ksock id of the binder
          * the name of the message being bound to by a Replier (terminated
            by a null byte, and then, if necessary, padded up to the next
            four-byte boundary with null bytes

        The default is False, i.e., not to output report such events.

        * if 'report_events' is true then we want bind/unbind messages.
        * if 'just_ask' is true, then we just want to find out the current state
          of the flag, and 'report_events' will be ignored.

        Returns the previous value of the flag (i.e., what it used to be set to).
        Which, if 'just_ask' is true, will also be the current state.

        Beware that setting this flag affects the Ksock as a whole, so it is
        possible for several programs to open a Ksock and "disagree" about how
        this flag should be set.
        """
        if just_ask:
            val = 0xFFFFFFFF
        elif report_events:
            val = 1
        else:
            val = 0
        id = array.array('L', [val])
        fcntl.ioctl(self.fd, Ksock.IOC_REPORTREPLIERBINDS, id, True)
        return id[0]

    def write_msg(self, message):
        """Write a Message. Doesn't send it.
        """
        # Message data is held in an array.array, and arrays know
        # how to write themselves out
        self.fd.write(message.msg)
        # But we are responsible for flushing
        self.fd.flush()

    def send_msg(self, message):
        """Write a Message, and then send it.

        Entirely equivalent to calling 'write_msg' and then 'send',
        and returns the MessageId of the sent message, as 'send' does.
        """
        self.write_msg(message)
        return self.send()

    def write_data(self, data):
        """Write out (and flush) some data.

        Does not send it, does not imply that it is all of a message
        (although clearly it should form *some* of a message).
        """
        ret = self.fd.write(data)
        self.fd.flush()
        return ret

    def read_msg(self, length):
        """Read a Message of length 'length' bytes.

        It is assumed that 'length' was returned by a previous call
        of 'next_msg'. It must be large enough to cause the entire
        message to be read.

        After the data has been read, it is passed to Message to
        construct a message instance, which is returned.

        Returns None if there was nothing to be read.
        """
        data = self.fd.read(length)
        if data:
            return Message(data)
        else:
            return None

    def read_next_msg(self):
        """Read the next Message.

        Equivalent to a call of 'next_msg', followed by reading the
        appropriate number of bytes and passing that to Message to
        construct a message instance, which is returned.

        Returns None if there was nothing to be read.
        """
        data = self.fd.read(self.next_msg())
        if data:
            return Message(data)
        else:
            return None

    def wait_for_msg(self, timeout=None):
        """Wait for the next Message.

        This is a simple wrapper around select.select, waiting for the
        next Message on this Ksock.

        If timeout is given, it is a floating point number of seconds,
        after which to timeout the select, otherwise this method will
        wait forever.

        Returns the new Message, or None if the timeout expired.
        """
        if timeout:
            (r, w, x) = select.select([self], [], [], timeout)
        else:
            (r, w, x) = select.select([self], [], [], timeout)

        return self.read_next_msg()

    def read_data(self, count):
        """Read the next 'count' bytes, and return them.

        Returns '' (the empty string) if there was nothing to be read,
        which is consistent with now Python file reads normally behave
        at end-of-file.
        """
        return self.fd.read(count)

    # It's modern times, so we really should implement "with"
    # (it's so convenient)

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

    # And what is a file-like object without iterator support?
    # Note that our iteration will stop when there is no next message
    # to read -- so trying to iterate again later on may work again...

    def __iter__(self):
        return self

    def next(self):
        msg = self.read_next_msg()
        if msg == None:
            raise StopIteration
        else:
            return msg

    def fileno(self):
        """Return the integer file descriptor from our internal fd.

        This allows a Ksock instance to be used in a call of select.select()
        - so, for instance, on should be able to do::

            (r, w, x) = select.select([ksock1, ksock2, ksock3], None, None)

        instead of the (less friendly, but also valid)::

            (r, w, x) = select.select([ksock1.fd, ksock2.fd, ksock3.fd], None, None)
        """
        return self.fd.fileno()

def read_bindings(names):
    """Read the bindings from /proc/kbus/bindings, and return a list

    /proc/kbus/bindings gives us data like::

            0: 10 16319 R $.Fred
            0: 11 17420 L $.Fred.Bob
            0: 12 17422 R $.William

    (i.e., device, file descriptor id, PID of process, whether it is Replier
    or Listener, and the message name concerned).

    'names' is a dictionary of file descriptor binding id to string (name)
    - for instance:

        { 10:'f1', 11:'f2' }

    If there is no entry in the 'names' dictionary for a given id, then the
    id will be used (as an integer).

    Thus with the above we would return a list of the form::

        [ ('f1', True, '$.Fred'), ('f2', False, '$.Fred.Bob'),
          (12, True, '$.William' ]
    """
    f = open('/proc/kbus/bindings')
    l = f.readlines()
    f.close()
    bindings = []
    for line in l:
        if line[0] == '#':
            continue
        # 'dev' is the device index (default is 0, may be 0..9 depending on how
        # many /dev/kbus<N> devices there are).
        # For the moment, we're going to ignore it.
        dev, id, pid, rep, name = line.split()
        id = int(id)
        if id in names:
            id = names[int(id)]
        if rep == 'R':          # Replier
            rep = True
        elif rep == 'L':        # (just a) Listener
            rep = False
        else:
            raise ValueError("Got replier '%c' when expecting 'R' or 'L'"%rep)
        bindings.append((id, rep, name))
    return bindings

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
