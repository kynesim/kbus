"""Python code for using and testing the kbus kernel module.

Intended for use with (for instance) nose -- so, for instance::

    $ cd kernel_module
    $ make
    $ nosetests kbus.py -d
    .
    ----------------------------------------------------------------------
    Ran 1 test in 0.026s

    OK

To get the doctests (for instance, in Message) as well, try::

    nosetests kbus.py -d --doctest-tests --with-doctest

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

import sys
import os
import subprocess
import nose
import fcntl
import time
import ctypes
import array
import errno

# Kernel definitions for ioctl commands
# Following closely from #include <asm[-generic]/ioctl.h>
# (and with some thanks to http://wiki.maemo.org/Programming_FM_radio/)
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
def _IOC(d,t,nr,size):
    return ((d << _IOC_DIRSHIFT) | (ord(t) << _IOC_TYPESHIFT) | 
            (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT))
def _IO(t,nr):
    return _IOC(_IOC_NONE, t, nr, 0)
def _IOW(t,nr,size):
    return _IOC(_IOC_WRITE, t, nr, size)
def _IOR(t,nr,size):
    return _IOC(_IOC_READ, t, nr, size)
def _IOWR(t,nr,size):
    return _IOC(_IOC_READ | _IOC_WRITE, t, nr, size)

KBUS_IOC_MAGIC = 'k'
KBUS_IOC_RESET	  = _IO(KBUS_IOC_MAGIC,   1)
KBUS_IOC_BIND	  = _IOW(KBUS_IOC_MAGIC,  2, ctypes.sizeof(ctypes.c_char_p))
KBUS_IOC_UNBIND	  = _IOW(KBUS_IOC_MAGIC,  3, ctypes.sizeof(ctypes.c_char_p))
KBUS_IOC_BOUNDAS  = _IOR(KBUS_IOC_MAGIC,  4, ctypes.sizeof(ctypes.c_char_p))
KBUS_IOC_REPLIER  = _IOWR(KBUS_IOC_MAGIC, 5, ctypes.sizeof(ctypes.c_char_p))
KBUS_IOC_NEXTLEN  = _IO(KBUS_IOC_MAGIC,   6)
KBUS_IOC_LASTSENT = _IOR(KBUS_IOC_MAGIC,  7, ctypes.sizeof(ctypes.c_char_p))

def BIT(nr):
    return 1L << nr

NUM_DEVICES = 3

def setup_module():
    retcode = system('sudo insmod kbus.ko kbus_num_devices=%d'%NUM_DEVICES)
    assert retcode == 0
    # Via the magic of hotplugging, that should cause our device to exist
    # ...eventually
    time.sleep(1)

def teardown_module():
    retcode = system('sudo rmmod kbus')
    assert retcode == 0
    # Via the magic of hotplugging, that should cause our device to go away
    # ...eventually
    time.sleep(1)
    assert not os.path.exists("/dev/kbus0")

# Let's be good and not use os.system...
def system(command):
    """Taken from the Python reference manual. Thank you.
    """
    try:
        retcode = subprocess.call(command, shell=True)
        if retcode < 0:
            print "'%s' was terminated by signal %s"%(command,-retcode)
        else:
            print "'%s' returned %s"%(command,retcode)
        return retcode
    except OSError, e:
        print "Execution of '%s' failed: %s"%(command,e)

class Message(object):
    """A wrapper for a KBUS message

    A Message can be created in a variety of ways. Perhaps most obviously:

        >>> msg1 = Message('$.Fred',data='1234')
        >>> msg1
        Message('$.Fred', data=array('L', [875770417L]), to=0L, from_=0L, in_reply_to=0L, flags=0x00000000, id=0L)

    Note that if the 'data' is specified as a string, there must be a multiple
    of 4 characters -- i.e., it must "fill out" an array of unsigned int-32
    words.

    A Message can be constructed from another message directly:

        >>> msg2 = Message(msg1)
        >>> msg2 == msg1
        True

    or from the '.extract()' tuple:

        >>> msg3 = Message(msg1.extract())
        >>> msg3 == msg1
        True

    or from an equivalent list::

        >>> msg3 = Message(list(msg1.extract()))
        >>> msg3 == msg1
        True

    Or one can use an array (of unsigned 32-bit words):

        >>> msg1.array
        array('L', [1937072747L, 0L, 0L, 0L, 0L, 0L, 6L, 1L, 1917201956L, 25701L, 875770417L, 1801614707L])
        >>> msg3 = Message(msg1.array)
        >>> msg3 == msg1
        True

    or the same thing represented as a string:

        >>> msg_as_string = msg1.array.tostring()
        >>> msg4 = Message(msg_as_string)
        >>> msg4 == msg1
        True

    For convenience, the parts of a Message may be retrieved as properties:

        >>> msg1.id
        0L
        >>> msg1.name
        '$.Fred'
        >>> msg1.to
        0L
        >>> msg1.from_
        0L
        >>> msg1.in_reply_to
        0L
        >>> msg1.flags
        0L
        >>> msg1.data
        array('L', [875770417L])

    The arguments to Message() are:

    - 'arg' -- this is the initial argument, and is a message name (a string
      that starts '$.'), a Message, some data that may be interpreted as a
      message (e.g., an array.array of unsigned 32-bit words, or a string of
      the right form), or a tuple from the .extract() method of another
      Message.

    If 'arg' is a message name, then the keyword arguments may be used (if
    'arg' is not a messsage name, they will be ignored):

    - 'data' is data for the Message, something that can be assigned to an
      array.array of unsigned 32-bit words.
    - 'to' is the Interface id for the destination, for use in replies or in
      stateful messaging. Normally it should be left 0.
    - 'from_' is the Interface id of the sender. Normally this should be left
      0, as it is assigned by KBUS.
    - if 'in_reply_to' is non-zero, then it is the Interface id to which the
      reply shall go (taken from the 'from_' field in the original message).
      Setting 'in_reply_to' non-zero indicates that the Message *is* a reply.
      See also the Reply class, which makes constructing replies simpler.
    - 'flags' can be used to set the flags for the message. If all that is
      wanted is to set Messages.WANT_A_REPLY flag, it is simpler to use the
      Request class to construct the message.
    - 'id' may be used to set the message id, although KBUS will ignore this
      and set the id internally (this can be useful when constructing a message
      to compare received messages against).

    Our internal values are:

    - 'array', which is the actual message data, as an array.array('L',...)
      (i.e., unsigned 32-bit words)
    - 'length', which is the length of that array (again, as 32-bit words)
    """

    START_GUARD = 0x7375626B
    END_GUARD   = 0x6B627573

    WANT_A_REPLY        = BIT(0)
    WANT_YOU_TO_REPLY   = BIT(1)

    # Header offsets (in case I change them again)
    IDX_START_GUARD     = 0
    IDX_ID              = 1
    IDX_IN_REPLY_TO     = 2
    IDX_TO              = 3
    IDX_FROM            = 4
    IDX_FLAGS           = 5
    IDX_NAME_LEN        = 6
    IDX_DATA_LEN        = 7     # required to be the last fixed item
    IDX_END_GUARD       = -1

    def __init__(self, arg, data=None, to=0, from_=0, in_reply_to=0, flags=0, id=0):
        """Initialise a Message.
        """

        if isinstance(arg,Message):
            self.array = array.array('L',arg.array)
        elif isinstance(arg,tuple) or isinstance(arg,list):
            # A tuple from .extract(), or an equivalent tuple/list
            if len(arg) != 7:
                raise ValueError("Tuple arg to Message() must have"
                        " 7 values, not %d"%len(arg))
            else:
                # See the extract() method for the order of args
                self.array = self._from_data(arg[-2],           # message name
                                             data=arg[-1],
                                             in_reply_to=arg[1],
                                             to=arg[2],
                                             from_=arg[3],
                                             flags=arg[4],
                                             id=arg[0])
        elif isinstance(arg,str) and arg.startswith('$.'):
            # It looks like a message name
            self.array = self._from_data(arg,data,to,from_,in_reply_to,flags,id)
        elif arg:
            # Assume it's sensible data...
            # (note that even if 'arg' was an array of the correct type.
            # we still want to take a copy of it, so the following is
            # reasonable enough)
            self.array = array.array('L',arg)
        else:
            raise ValueError,'Argument %s does not seem to make sense'%repr(arg)

        # Make sure the result *looks* like a message
        self._check()

        # And I personally find it useful to have the length available
        self.length = len(self.array)

    def _from_data(self,name,data=None,to=0,from_=0,in_reply_to=0,flags=0,id=0):
        """Set our data from individual arguments.

        Note that 'data' must be:
        
        1. an array.array('L',...) instance, or
        2. a string, or something else compatible, which will be converted to
           the above, or
        3. None.
        """

        msg = array.array('L',[])

        # Start guard, id, to, from, flags -- all defaults for the moment
        msg.append(self.START_GUARD)    # start guard
        msg.append(id)                  # remember KBUS will overwrite the id
        msg.append(in_reply_to)
        msg.append(to)
        msg.append(from_)
        msg.append(flags)

        # We add the *actual* length of the name
        msg += array.array('L',[len(name)])
        # But remember that the message name itself needs padding out to
        # 4-bytes
        # ...this is about the nastiest way possible of doing it...
        while len(name)%4:
            name += '\0'

        # If it's not already an array of the right type, then let's try and
        # make it so
        if data == None:
            pass
        elif isinstance(data,array.array) and data.typecode == 'L':
            pass
        else:
            data = array.array('L',data)

        # Next comes data length (which we now know will be in the right units)
        if data:
            msg.append(len(data))
        else:
            msg.append(0)

        # Then the name
        name_array = array.array('L',name)
        msg += name_array

        # And, if we have any, the data
        if data:
            msg += data

        # And finally remember the end guard
        msg.append(self.END_GUARD)

        return msg

    def _check(self):
        """Perform some basic sanity checks on our data.
        """
        # XXX Make the reporting of problems nicer for the user!
        assert self.array[self.IDX_START_GUARD] == self.START_GUARD
        assert self.array[self.IDX_END_GUARD] == self.END_GUARD
        name_len_bytes = self.array[self.IDX_NAME_LEN]
        name_len = (name_len_bytes+3) / 4        # in 32-bit words
        data_len = self.array[self.IDX_DATA_LEN] # in 32-bit words
        if name_len_bytes < 3:
            raise ValueError("Message name is %d long, minimum is 3"
                             " (e.g., '$.*')"%name_len_bytes)
        assert data_len >= 0
        assert (self.IDX_DATA_LEN + 2) + name_len + data_len == len(self.array)

    def __repr__(self):
        (id,in_reply_to,to,from_,flags,name,data_array) = self.extract()
        args = [repr(name),
                'data='+repr(data_array),
                'to='+repr(to),
                'from_='+repr(from_),
                'in_reply_to='+repr(in_reply_to),
                'flags=0x%08x'%flags,
                'id='+repr(id)]
        return 'Message(%s)'%(', '.join(args))

    def __eq__(self,other):
        if not isinstance(other,Message):
            return False
        else:
            return self.array == other.array

    def __ne__(self,other):
        if not isinstance(other,Message):
            return True
        else:
            return self.array != other.array

    def equivalent(self,other):
        """Returns true if the two messages only differ in 'id', 'in_reply_to' and 'from'
        """
        if self.length != other.length:
            return False
        # Somewhat clumsily...
        parts1 = list(self.extract())
        parts2 = list(other.extract())
        parts1[0] = parts2[0]   # id
	parts1[1] = parts2[1]   # in_reply_to
	parts1[3] = parts2[3]   # from_
        return parts1 == parts2

    def set_want_reply(self,value=True):
        """Set or unset the 'we want a reply' flag.
        """
        if value:
            self.array[self.IDX_FLAGS] = self.array[self.IDX_FLAGS] | Message.WANT_A_REPLY
        elif self.array[self.IDX_FLAGS] & Message.WANT_A_REPLY:
            mask = ~Message.WANT_A_REPLY
            self.array[self.IDX_FLAGS] = self.array[self.IDX_FLAGS] & mask

    def should_reply(self):
        """Return true if we're meant to reply to this message.
        """
        return self.array[self.IDX_FLAGS] & Message.WANT_YOU_TO_REPLY

    def _get_id(self):
        return self.array[self.IDX_ID]

    def _get_in_reply_to(self):
        return self.array[self.IDX_IN_REPLY_TO]

    def _get_to(self):
        return self.array[self.IDX_TO]

    def _get_from(self):
        return self.array[self.IDX_FROM]

    def _get_flags(self):
        return self.array[self.IDX_FLAGS]

    def _get_name(self):
        name_len = self.array[self.IDX_NAME_LEN]
        name_array_len = (name_len+3)/4         # i.e., 32-bit words

        base = self.IDX_DATA_LEN + 1

        name_array = self.array[base:base+name_array_len]
        name = name_array.tostring()
        # Make sure we remove the padding bytes (although they *should* be
        # '\0', and so "reasonably safe")
        return name[:name_len]

    def _get_data(self):
        name_len = self.array[self.IDX_NAME_LEN]
        data_len = self.array[self.IDX_DATA_LEN]

        name_array_len = (name_len+3)/4         # i.e., 32-bit words

        base = self.IDX_DATA_LEN + 1

        data_offset = base+name_array_len
        return self.array[data_offset:data_offset+data_len]

    id          = property(_get_id)
    in_reply_to = property(_get_in_reply_to)
    to          = property(_get_to)
    from_       = property(_get_from)
    flags       = property(_get_flags)
    name        = property(_get_name)
    data        = property(_get_data)

    def extract(self):
        """Return our parts as a tuple.

        The values are returned in something approximating the order
        within the message itself:

            (id,in_reply_to,to,from_,flags,name,data_array)

        This is not the same order as the keyword arguments to Message().
        """

        # Sanity check:
        assert self.array[self.IDX_START_GUARD] == self.START_GUARD
        assert self.array[self.IDX_END_GUARD] == self.END_GUARD

        return (self.id, self.in_reply_to, self.to, self.from_,
                self.flags, self.name, self.data)

class Request(Message):
    """A message that wants a reply.

    This is intended to be a convenient way of constructing a message that
    wants a reply.

    For instance:

        >>> msg = Message('$.Fred',data='abcd',flags=Message.WANT_A_REPLY)
        >>> msg
        Message('$.Fred', data=array('L', [1684234849L]), to=0L, from_=0L, in_reply_to=0L, flags=0x00000001, id=0L)
        >>> req = Request('$.Fred',data='abcd')
        >>> req
        Message('$.Fred', data=array('L', [1684234849L]), to=0L, from_=0L, in_reply_to=0L, flags=0x00000001, id=0L)
        >>> req == msg
        True

    Note that:

    1. A Request instance still represents itself as a Message. This is
       perhaps not ideal, but is consistent with how Reply instances work.
    2. A request message is a request just because it has the
       Message.WANT_A_REPLY flag set. There is nothing else special about it.
    """

    def __init__(self, arg, **kwargs):
        """Arguments are exactly the same as for Message itself.
        """
        # First, just do what the caller asked for directly
        super(Request,self).__init__(arg, **kwargs)
        # But then make sure that the "wants a reply" flags is set
        self.set_want_reply()

class Reply(Message):
    """A reply message.

    This is intended to be the normal way of constructing a reply message.

    For instance:

        >>> msg = Message('$.Fred',data='abcd',from_=27,to=99,id=132,flags=Message.WANT_A_REPLY)
        >>> msg
        Message('$.Fred', data=array('L', [1684234849L]), to=99L, from_=27L, in_reply_to=0L, flags=0x00000001, id=132L)
        >>> reply = Reply(msg)
        >>> reply
        Message('$.Fred', data=array('L'), to=27L, from_=0L, in_reply_to=132L, flags=0x00000000, id=0L)

    Note that:

    1. A Reply instance still represents itself as a Message. This is mostly
       because the representation can be used to construct an identical
       Message -- doing this with a Reply() would mean including the
       representation of the original Message as well.
    2. A reply message is a reply because it has the 'in_reply_to' field set.
       This indicates the message id of the original message, the one we're
       replying to.
    3. As normal, the Reply's own message id is unset - KBUS will set this, as
       for any message.
    4. We give a specific 'to' value, the id of the interface that sent the
       original message, and thus the 'from' value in the original message.
    5. We keep the same message name, but don't copy the original message's
       data. If we want to send data in a reply message, it will be our own
       data.
    """

    def __init__(self, original, data=None):
        (id,in_reply_to,to,from_,flags,name,data_array) = original.extract()
        # We reply to the original sender (to), indicating which message we're
        # responding to (in_reply_to).
        # The fact that in_reply_to is set means that we *are* a reply.
        # We don't need to set any flags.
        super(Reply,self).__init__(name, data=data,
                                   in_reply_to=id,
                                   to=from_)

class KbufBindStruct(ctypes.Structure):
    """The datastucture we need to describe a KBUS_IOC_BIND argument
    """
    _fields_ = [('replier',    ctypes.c_uint),
                ('guaranteed', ctypes.c_uint),
                ('len',        ctypes.c_uint),
                ('name',       ctypes.c_char_p)]

class KbufListenerStruct(ctypes.Structure):
    """The datastucture we need to describe a KBUS_IOC_REPLIER argument
    """
    _fields_ = [('return_id', ctypes.c_uint),
                ('len',  ctypes.c_uint),
                ('name', ctypes.c_char_p)]

class Interface(object):
    """A wrapper around a KBUS device, for purposes of message sending.

    'which' is which KBUS device to open -- so if 'which' is 3, we open
    /dev/kbus3.

    'mode' should be 'r' or 'rw' -- i.e., whether to open the device for read or
    write (opening for write also allows reading, of course).

    I'm not really very keen on the name Interface, but it's better than the
    original "File", which I think was actively misleading.
    """

    def __init__(self,which=0,mode='r'):
        if mode not in ('r','rw'):
            raise ValueError("Interface mode should be 'r' or 'rw', not '%s'"%mode)
        self.which = which
        self.name = '/dev/kbus%d'%which
        if mode == 'r':
            self.mode = 'read'
        else:
            mode = 'w+'
            self.mode = 'read/write'
        # Although Unix doesn't mind whether a file is opened with a 'b'
        # for binary, it is possible that some version of Python may
        self.fd = open(self.name,mode+'b')

    def __repr__(self):
        if self.fd:
            return '<Interface %s open for %s>'%(self.name,self.mode)
        else:
            return '<Interface %s closed>'%(self.name)

    def close(self):
        ret = self.fd.close()
        self.fd = None
        self.mode = None
        return ret

    def bind(self,name,replier=False,guaranteed=False):
        """Bind the given name to the file descriptor.

        If 'replier', then we are binding as the only fd that can reply to this
        message name.

        If 'guaranteed', then we require that *all* messages to us be delivered,
        otherwise kbus may drop messages if necessary.
        """
        arg = KbufBindStruct(replier,guaranteed,len(name),name)
        return fcntl.ioctl(self.fd, KBUS_IOC_BIND, arg);

    def unbind(self,name,replier=False,guaranteed=False):
        """Unbind the given name from the file descriptor.

        The arguments need to match the binding that we want to unbind.
        """
        arg = KbufBindStruct(replier,guaranteed,len(name),name)
        return fcntl.ioctl(self.fd, KBUS_IOC_UNBIND, arg);

    def bound_as(self):
        """Return the 'bind number' for this file descriptor.
        """
        # Instead of using a ctypes.Structure, we can retrieve homogenious
        # arrays of data using, well, arrays. This one is a bit minimalist.
        id = array.array('L',[0])
        fcntl.ioctl(self.fd, KBUS_IOC_BOUNDAS, id, True)
        return id[0]

    def next_len(self):
        """Return the length of the next message (if any) on this file descriptor
        """
        return fcntl.ioctl(self.fd, KBUS_IOC_NEXTLEN, 0)

    def last_msg_id(self):
        """Return the id of the last message written on this file descriptor.

        Returns 0 before any messages have been sent.
        """
        id = array.array('L',[0])
        fcntl.ioctl(self.fd, KBUS_IOC_LASTSENT, id, True)
        return id[0]

    def find_listener(self,name):
        """Find the id of the replier (if any) for this message.

        Returns None if there was no replier, otherwise the replier's id.
        """
        arg = KbufListenerStruct(0,len(name),name)
        retval = fcntl.ioctl(self.fd, KBUS_IOC_REPLIER, arg);
        if retval:
            return arg.return_id
        else:
            return None

    def write(self,message):
        """Write a Message.
        """
        # Message data is held in an array.array, and arrays know
        # how to write themselves out
        message.array.tofile(self.fd)
        # But we are responsible for flushing
        self.fd.flush()

    def read(self):
        """Read the next Message.

        Returns None if there was nothing to be read.
        """
        data = self.fd.read(self.next_len())
        if data:
            return Message(data)
        else:
            return None

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
        msg = self.read()
        if msg == None:
            raise StopIteration
        else:
            return msg

class BindingsMemory(object):
    """A class for remembering message name bindings.

    We remember bindings in a dictionary, relating Interface instances to
    bindings made on those interfaces. So, for instance:
    
       bindings[if] = [(True,False,'$.Fred.Jim.Bob'),
                       (False,True,'$.Fred')]
    
    (the order in the tuple matches that in the /proc/kbus/bindings file).
    
    Automatically managed by the local bind and unbind *methods*
    """

    def __init__(self):
        self.bindings = {}

    def remember_interface(self,interface):
        self.bindings[interface] = []

    def forget_interface(self,interface):
        del self.bindings[interface]

    def remember_binding(self,interface,name,replier=False,guaranteed=False):
        self.bindings[interface].append( (replier,guaranteed,name) )

    def forget_binding(self,interface,name,replier=False,guaranteed=False):
        if_list = self.bindings[interface]
        # If there are multiple matches, we'll delete the first,
        # which is what we want (well, to delete a single instance)
        for index,thing in enumerate(if_list):
            if thing[-1] == name:       # the name is always the last element
                del if_list[index]
                break
        # No matches shouldn't occur, but let's ignore it anyway

    def check_bindings(self):
        """Check the bindings we think we have match those of kbus
        """
        expected = []
        for interface,if_list in self.bindings.items():
            for r,a,n in if_list:
                expected.append( (interface,r,a,n) )
        assert bindings_match(expected)

class RecordingInterface(Interface):
    """A variant of Interface which remembers and checks its bindings.

    Intended originally for use in writing test code.

    The constructor takes an extra argument, which should be a BindingsMemory
    instance, and which is used to remember our bindings. Otherwise, use it
    just like an ordinary Interface.
    """

    def __init__(self, which=0, mode='r', bindings=None):
        super(RecordingInterface,self).__init__(which,mode)
        self.bindings = bindings
        self.bindings.remember_interface(self)

    def close(self):
        super(RecordingInterface,self).close()
        self.bindings.forget_interface(self)
        self.bindings = None

    def bind(self,name,replier=False,guaranteed=False):
        """A wrapper around the 'bind' function. to keep track of bindings.
        """
        super(RecordingInterface,self).bind(name,replier,guaranteed)
        self.bindings.remember_binding(self,name,replier,guaranteed)

    def unbind(self,name,replier=False,guaranteed=False):
        """A wrapper around the 'unbind' function, to keep track of bindings.
        """
        super(RecordingInterface,self).unbind(name,replier,guaranteed)
        self.bindings.forget_binding(self,name,replier,guaranteed)

def read_bindings(names):
    """Read the bindings from /proc/kbus/bindings, and return a list

    /proc/kbus/bindings gives us data like::

            0: 10 R T $.Fred
            0: 11 L T $.Fred.Bob
            0: 12 R F $.William

    'names' is a dictionary of file descriptor binding id to string (name)
    - for instance:
    
        { 10:'f1', 11:'f2' }

    If there is no entry in the 'names' dictionary for a given id, then the
    id will be used (as an integer).
        
    Thus with the above we would return a list of the form::

        [ ('f1',True,True,'$.Fred'), ('f2',False,True,'$.Fred.Bob'),
          (12,True,False,'$.William' ]
    """
    f = open('/proc/kbus/bindings')
    l = f.readlines()
    f.close()
    bindings = []
    for line in l:
        # 'dev' is the device index (default is 0, may be 0..9 depending on how
        # many /dev/kbus<N> devices there are).
        # For the moment, we're going to ignore it.
        dev,id,rep,all,name = line.split()
        id = int(id)
        if id in names:
            id = names[int(id)]
        if rep == 'R':          # Replier
            rep = True
        elif rep == 'L':        # (just a) Listener
            rep = False
        else:
            raise ValueError,"Got replier '%c' when expecting 'R' or 'L'"%rep
        if all == 'T':          # Want ALL messages
            all = True
        elif all == 'F':        # Willing to miss some messages
            all = False
        else:
            raise ValueError,"Got all '%c' when expecting 'T' or 'F'"%all
        bindings.append((id,rep,all,name))
    return bindings

def str_rep(rep):
    if rep:
        return 'R'
    else:
        return 'L'

def str_all(all):
    if all:
        return 'T'
    else:
        return 'F'

def bindings_match(bindings):
    """Look up the current bindings and check they match the list.

    'bindings' is a sequence of tuples, each of the form:

        ( file_descriptor, True|False, True|False, name )

    so for instance:

        ( (f,True,True,'$.Fred'), (g,False,False,'$.JimBob') )

    where the first True means the binding is for a replier (or not), and the
    second means it wants to guarantee to receive all its messages (or not).

    The function reads the contents of /proc/kbus/bindings. It translates each
    file descriptor to a listener id using ``bound_as``, and thus converts
    'bindings' to an equivalent list.

    Silently returns True if the bindings in /proc/kbus/bindings match
    those expected, returns False (and prints out the mismatch) if they do not.
    """
    testwith = []
    names = {}
    for (fd,rep,all,name) in bindings:
        if fd not in names:
            names[fd] = fd.bound_as()
        testwith.append((fd.bound_as(),rep,all,name))

    actual = read_bindings(names)

    # And compare the two lists - ideally they should match
    # (although we don't want to care about order, I think)
    actual.sort()
    testwith.sort()
    if actual == testwith:
        return True

    # If they're not the same, we need to let the user know in some not too
    # unfriendly manner
    found    = set(actual)
    expected = set(testwith)
    print 'The contents of /proc/kbus/bindings is not as expected'
    if len(found):
        print 'The following were expected but not found:'
        for f,r,a,n in expected-found:
            print '  %10u %c %c %s'%(f,str_rep(r),str_all(a),n)
    if len(expected):
        print 'The following were found but not expected:'
        for f,r,a,n in found-expected:
            print '  %10u %c %c %s'%(f,str_rep(r),str_all(a),n)
    return False

def check_IOError(expected_errno,fn,*stuff):
    """When calling apply(fn,stuff), check for IOError with the given errno.

    Check that is what happens...
    """
    try:
        apply(fn,stuff)
        # We're not expecting to get here...
        assert False, 'Applying %s%s did not fail with IOError'%(repr(fn),repr(stuff))
    except IOError, e:
        actual_errno = e.args[0]
        errno_name = errno.errorcode[actual_errno]
        expected_errno_name = errno.errorcode[expected_errno]
        assert actual_errno == expected_errno, \
                'expected %s, got %s'%(expected_errno_name,errno_name)
    except Exception, e:
        print e
        assert False, 'Applying %s%s failed with %s, not IOError'%(repr(fn),
                repr(stuff),sys.exc_type)

class TestInterface:
    """Some basic testing of Interface.

    Not much here, because most of its testing is done implicitly via
    its use in other tests. And it really is fairly simple.
    """

    def test_opening(self):
        """Test opening/closing Interface objects.
        """
        # We should be able to open each device that exists
        for ii in range(NUM_DEVICES):
            f = Interface(ii)
            f.close()
        # and not those that don't
        check_IOError(errno.ENOENT,Interface,-1)
        check_IOError(errno.ENOENT,Interface,NUM_DEVICES)

    def test_modes(self):
        """Test only the allowed modes are allowed
        """
        f = Interface(0,'r')
        f.close()
        f = Interface(0,'rw')
        f.close()
        nose.tools.assert_raises(ValueError,Interface,0,'fred')
        nose.tools.assert_raises(ValueError,Interface,0,'w+')
        nose.tools.assert_raises(ValueError,Interface,0,'x')

class TestKernelModule:

    def __init__(self):
        self.bindings = BindingsMemory()

    def _check_bindings(self):
        self.bindings.check_bindings()

    def _check_read(self,f,expected):
        """Check that we can read back an equivalent message to 'expected'
        """
        if expected:
            new_message = f.read()
            assert new_message != None
            assert expected.equivalent(new_message)
        else:
            # We're not expecting anything -- check that's what we get
            # - first, directly
            data = f.fd.read(1)
            assert data == ''
            # - secondly, in terms of Interface and Message
            assert f.read() == None

    def test_readonly(self):
        """If we open the device readonly, we can't do much(!)
        """
        f = RecordingInterface(0,'r',self.bindings)
        assert f != None
        try:
            # Nothing to read
            assert f.read() == None

            # We can't write to it
            msg2 = Message('$.Fred','data')
            check_IOError(errno.EBADF,f.write,msg2)
        finally:
            assert f.close() is None

    def test_readwrite_kbus0(self):
        """If we open the device read/write, we can read and write.
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None

        try:
            f.bind('$.B')
            f.bind('$.C')

            # We start off with no message
            self._check_read(f,None)

            # We can write a message and read it back
            msg1 = Message('$.B','data')
            f.write(msg1)
            self._check_read(f,msg1)

            # We can write a message and read it back, again
            msg2 = Message('$.C','fred')
            f.write(msg2)
            self._check_read(f,msg2)

            # If we try to write a message that nobody is listening for,
            # we get an appropriate error
            msg3 = Message('$.D','fred')
            check_IOError(errno.EADDRNOTAVAIL,f.write,msg3)

        finally:
            assert f.close() is None

    def test_two_opens_kbus0(self):
        """If we open the device multiple times, they communicate
        """
        f1 = RecordingInterface(0,'rw',self.bindings)
        assert f1 != None
        try:
            f2 = RecordingInterface(0,'rw',self.bindings)
            assert f2 != None
            try:
                # Both files listen to both messages
                f1.bind('$.B',False)
                f1.bind('$.C',False)
                f2.bind('$.B',False)
                f2.bind('$.C',False)

                # Nothing to read at the start
                self._check_read(f1,None)
                self._check_read(f2,None)

                # If we write, we can read appropriately
                msg1 = Message('$.B','data')
                f1.write(msg1)
                self._check_read(f2,msg1)
                self._check_read(f1,msg1)

                msg2 = Message('$.C','data')
                f2.write(msg2)
                self._check_read(f1,msg2)
                self._check_read(f2,msg2)
            finally:
                assert f2.close() is None
        finally:
            assert f1.close() is None

    def test_bind(self):
        """Initial ioctl/bind test.
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None

        try:
            # - BIND
            # Low level check: The "Bind" ioctl requires a proper argument
            check_IOError(errno.EINVAL, fcntl.ioctl, f.fd, KBUS_IOC_BIND, 0)
            # Said string must not be zero length
            check_IOError(errno.EBADMSG, f.bind, '', True)
            # At some point, it will have restrictions on what it *should* look
            # like
            f.bind('$.Fred')
            # - UNBIND
            check_IOError(errno.EINVAL, fcntl.ioctl, f.fd, KBUS_IOC_UNBIND, 0)
            check_IOError(errno.EBADMSG, f.unbind, '', True)
            f.unbind('$.Fred')
        finally:
            assert f.close() is None

    def test_many_bind_1(self):
        """Initial ioctl/bind test -- make lots of bindings
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None

        try:
            f.bind('$.Fred')
            f.bind('$.Fred.Jim')
            f.bind('$.Fred.Bob')
            f.bind('$.Fred.Jim.Bob')
        finally:
            assert f.close() is None

    def test_many_bind_2(self):
        """Initial ioctl/bind test -- make lots of the same binding
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None

        try:
            f.bind('$.Fred')
            f.bind('$.Fred',False)
            f.bind('$.Fred',False)
            f.bind('$.Fred',False)
        finally:
            assert f.close() is None

    def test_many_bind_3(self):
        """Initial ioctl/bind test -- multiple matching bindings/unbindings
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None

        try:
            f.bind('$.Fred',True)  # But remember, only one replier
            f.bind('$.Fred',False)
            f.bind('$.Fred',False)
            f.unbind('$.Fred',True)
            f.unbind('$.Fred',False)
            f.unbind('$.Fred',False)
            # But not too many
            check_IOError(errno.EINVAL, f.unbind, '$.Fred')
            check_IOError(errno.EINVAL, f.unbind, '$.Fred',False)
            # We can't unbind something we've not bound
            check_IOError(errno.EINVAL, f.unbind, '$.JimBob',False)
        finally:
            assert f.close() is None

    def test_bind_more(self):
        """Initial ioctl/bind test - with more bindings.
        """
        f1 = RecordingInterface(0,'rw',self.bindings)
        assert f1 != None
        try:
            f2 = RecordingInterface(0,'rw',self.bindings)
            assert f2 != None
            try:
                # We can bind and unbind
                f1.bind('$.Fred',replier=True)
                f1.unbind( '$.Fred',replier=True)
                f1.bind('$.Fred',replier=False)
                f1.unbind( '$.Fred',replier=False)
                # We can bind many times
                f1.bind('$.Fred',replier=False)
                f1.bind('$.Fred',replier=False)
                f1.bind('$.Fred',replier=False)
                # But we can only have one replier
                f1.bind('$.Fred',replier=True)
                check_IOError(errno.EADDRINUSE, f1.bind, '$.Fred',True)

                # Two files can bind to the same thing
                f1.bind('$.Jim.Bob',replier=False)
                f2.bind('$.Jim.Bob',replier=False)
                # But we can still only have one replier
                f1.bind('$.Jim.Bob',replier=True)
                check_IOError(errno.EADDRINUSE, f2.bind, '$.Jim.Bob', True)

                # Oh, and not all messages need to be received
                # - in our interfaces, we default to allowing kbus to drop
                # messages if necessary
                f1.bind('$.Jim.Bob',replier=False,guaranteed=True)
                f1.bind('$.Fred',replier=False,guaranteed=True)
            finally:
                assert f2.close() is None
        finally:
            assert f1.close() is None

    def test_bindings_match1(self):
        """Check that bindings match inside and out.
        """
        f1 = RecordingInterface(0,'rw',self.bindings)
        assert f1 != None
        try:
            f2 = RecordingInterface(0,'rw',self.bindings)
            assert f2 != None
            try:
                f1.bind('$.Fred',True)
                f1.bind('$.Fred.Jim',True)
                f1.bind('$.Fred.Bob',True)
                f1.bind('$.Fred.Jim.Bob',True)
                f1.bind('$.Fred.Jim.Derek')
                # /proc/kbus/bindings should reflect all of the above, and none other
                self._check_bindings()
                f2.bind('$.Fred.Jim.Derek')
                f2.bind('$.William')
                f2.bind('$.William')
                f2.bind('$.William')
                f1.bind('$.Fred.Jim.Bob.Eric',True)
                self._check_bindings()
            finally:
                assert f2.close() is None
        finally:
            assert f1.close() is None
        # And now all of the bindings *should* have gone away
        self._check_bindings()

    def test_rw_single_file(self):
        """Test reading and writing two messages on a single file
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None
        try:

            name1 = '$.Fred.Jim'
            data1 = array.array('L','datadata')

            name2 = '$.Fred.Bob.William'
            data2 = array.array('L','This is surely some data')

            # Bind so that we can write/read the first, but not the second
            f.bind(name1,True)
            f.bind('$.William',True)

            msg1 = Message(name1,data=data1)
            f.write(msg1)
            print 'Wrote:',msg1

            # There are no listeners for '$.Fred.Bob.William'
            msg2 = Message(name2,data=data2)
            check_IOError(errno.EADDRNOTAVAIL, f.write, msg2)

            msg1r = f.read()
            print 'Read: ',msg1r

            # The message read should essentially match
            assert msg1.equivalent(msg1r)

            msg2r = f.read()
            assert msg2r == None

            # There shouldn't be anything else to read
            assert f.read() == None

        finally:
            assert f.close() is None

    def test_read_write_2files(self):
        """Test reading and writing between two files.
        """
        f1 = RecordingInterface(0,'rw',self.bindings)
        assert f1 != None
        try:
            f2 = RecordingInterface(0,'rw',self.bindings)
            assert f2 != None
            try:
                f1.bind('$.Fred',True)
                f1.bind('$.Fred',False)
                f1.bind('$.Fred',False)

                f2.bind('$.Jim',True)

                # Writing to $.Fred on f1 - writes message id N
                msgF = Message('$.Fred','data')
                f1.write(msgF)
                n = f1.last_msg_id()

                # No one is listening for $.William
                msgW = Message('$.William')
                check_IOError(errno.EADDRNOTAVAIL, f1.write, msgW)
                check_IOError(errno.EADDRNOTAVAIL, f2.write, msgW)
                # (and attempting to write it doesn't increment KBUS's
                # counting of the message id)

                # Writing to $.Jim on f1 - writes message N+1
                msgJ = Message('$.Jim','moredata')
                f1.write(msgJ)
                assert f1.last_msg_id() == n+1

                # Reading f1 - message N
                assert f1.next_len() == msgF.length*4
                # By the way - it's still the next length until we read
                assert f1.next_len() == msgF.length*4
                data = f1.read()
                # Extract the message id -- this is N
                n0 = data.extract()[0]
                assert n == n0

                # Reading f2 - should be message N+1
                assert f2.next_len() == msgJ.length*4
                data = f2.read()
                n3 = data.extract()[0]
                assert n3 == n0+1

                # Reading f1 - should be message N again
                assert f1.next_len() == msgF.length*4
                data = f1.read()
                n1 = data.extract()[0]
                assert n1 == n0

                # Reading f1 - should be message N again
                assert f1.next_len() == msgF.length*4
                data = f1.read()
                n2 = data.extract()[0]
                assert n2 == n0

                # No more messages on f1
                assert f1.next_len() == 0
                assert f1.read() == None

                # No more messages on f2
                assert f2.next_len() == 0
                assert f2.read() == None
            finally:
                assert f2.close() is None
        finally:
            assert f1.close() is None

    def test_reply_single_file(self):
        """Test replying with a single file
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None
        try:

            name1 = '$.Fred.Jim'
            name2 = '$.Fred.Bob.William'
            name3 = '$.Fred.Bob.Jonathan'

            f.bind(name1,True)     # replier
            f.bind(name2,True)     # replier
            f.bind(name3,False)    # just listener


            msg1 = Message(name1,data='dat1')
            msg2 = Request(name2,data='dat2')
            msg3 = Request(name3,data='dat3')

            f.write(msg1)
            f.write(msg2)
            f.write(msg3)

            m1 = f.read()
            m2 = f.read()
            m3 = f.read()

            # For message 1, there is no reply needed
            assert not m1.should_reply()

            # For message 2, a reply is wanted, and we are the replier
            assert m2.should_reply()

            # For message 3, a reply is wanted, and we are not the replier
            assert not m1.should_reply()

            # So, we should reply to message 2 - let's do so

            # We can make a reply "by hand"
            (id,in_reply_to,to,from_,flags,name,data_array) = msg2.extract()
            reply_by_hand = Message(name, data=None, in_reply_to=id, to=from_)

            # But it is easier to use the pre-packaged mechanism
            reply = Reply(msg2)

            # These should, however, give the same result
            assert reply == reply_by_hand

            # And the obvious thing to do with a reply is
            f.write(reply)

            # And we should be able to read it...
            m4 = f.read()
            assert m4.equivalent(reply)

            # And there shouldn't be anything else to read
            assert f.next_len() == 0
        finally:
            assert f.close() is None

    def test_message_names(self):
        """Test for message name legality.
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None
        try:

            def _error(error,name):
                check_IOError(error, f.bind, name, True)

            def _ok(name):
                f.bind(name,True)

            # I don't necessarily know what name will be "too long",
            # but we can make a good guess as to a silly sort of length
            _error(errno.ENAMETOOLONG,'1234567890'*1000)

            # We need a leading '$.'
            _error(errno.EBADMSG,'')
            _error(errno.EBADMSG,'$')
            _error(errno.EBADMSG,'$x')
            _error(errno.EBADMSG,'Fred')

            _error(errno.EBADMSG,"$.Non-alphanumerics aren't allowed")
            _error(errno.EBADMSG,'$.#')

            # We cannot end with a dot
            _error(errno.EBADMSG,'$.Fred.')
            _error(errno.EBADMSG,'$.Fred..')
            # Or have two dots in a row
            _error(errno.EBADMSG,'$.Fred..Jim')
            _error(errno.EBADMSG,'$.Fred...Jim')
            _error(errno.EBADMSG,'$.Fred....Jim')

            # The following *are* legal
            _ok('$.Fred.Jim')
            _ok('$.Fred.Jim.Fred.Jim.MoreNames.And.More')
            _ok('$.QuiteLongWordsAreAllowedInNames')
            # Case matters
            _ok('$.This.is.a.different.name')
            _ok('$.THIS.is.a.different.name')
            # Top level wildcards are OK
            _ok('$.*')
            _ok('$.%')
        
        finally:
            assert f.close() is None

    def test_data_too_long(self):
        """Test for message name legality.
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None
        try:
            # I don't necessarily know how much data will be "too long",
            # but we can make a good guess as to a silly sort of length
            m = Message('$.Fred',data='12345678'*1000)
            f.bind('$.Fred')
            check_IOError(errno.EMSGSIZE, f.write, m)
        finally:
            assert f.close() is None

    def test_cant_write_to_wildcard(self):
        """It's not possible to write a message with a wildcard name.
        """
        f = RecordingInterface(0,'rw',self.bindings)
        assert f != None
        try:
            # Listen with a wildcard - this is OK
            f.bind('$.Fred.*')
            # Create a message with a silly name - Message doesn't care
            m = Message('$.Fred.*')
            # Try to write it -- this shall not work
            check_IOError(errno.EBADMSG, f.write, m)
            # Try a different wildcard
            f.bind('$.Jim.%')
            m = Message('$.Jim.%')
            check_IOError(errno.EBADMSG, f.write, m)
        finally:
            assert f.close() is None

    def test_wildcards_a_bit(self):
        """Some initial testing of wildcards. And use of 'with'
        """
        with RecordingInterface(0,'rw',self.bindings) as f:
            assert f != None
            f.bind('$.Fred.*',True)

            m = Message('$.Fred.Jim')
            f.write(m)
            r = f.read()
            assert r.equivalent(m)

            m = Message('$.Fred.JimBob.William')
            f.write(m)
            r = f.read()
            assert r.equivalent(m)

            m = Message('$.Fred')
            check_IOError(errno.EADDRNOTAVAIL, f.write, m)

            # A more specific binding, overlapping the wildcard
            f.bind('$.Fred.Jim',True)
            m = Message('$.Fred.Jim')
            f.write(m)
            r = f.read()
            assert r.equivalent(m)
            r = f.read()
            assert r.equivalent(m)

    def test_message_equality(self):
        """Messages are not equal to non-messages, and so on.
        """
        a = Message('$.Fred')
        b = Message('$.Fred')
        c = Message('$.Jim')
        assert (a == b)
        assert (a != c)

        assert (a ==    3) == False
        assert (a == None) == False
        assert (a !=    3) == True
        assert (a != None) == True

        assert (3    == a) == False
        assert (None == a) == False
        assert (3    != a) == True
        assert (None != a) == True

    def test_iteration(self):
        """Test we can iterate over messages.
        """
        with RecordingInterface(0,'rw',self.bindings) as f:
            assert f != None
            f.bind('$.Fred')
            m = Message('$.Fred')
            for ii in range(5):
                f.write(m)
            count = 0
            for r in f:
                count += 1
            assert count == 5
            # And again
            for ii in range(5):
                f.write(m)
            count = 0
            for r in f:
                count += 1
            assert count == 5

    def test_wildcard_listening_1(self):
        """Test using wildcards to listen - 1, asterisk.
        """
        with RecordingInterface(0,'rw',self.bindings) as f0:

            with RecordingInterface(0,'r',self.bindings) as f1:
                f1.bind('$.This.Fred')
                f1.bind('$.That.Fred')

                with RecordingInterface(0,'r',self.bindings) as f2:
                    f2.bind('$.This.Jim.One')
                    f2.bind('$.This.Jim.Two')
                    f2.bind('$.That.Jim')

                    with RecordingInterface(0,'r',self.bindings) as f3:
                        f3.bind('$.This.*')

                        # For each tuple, we have:
                        #
                        # 1. The Interface we're meant to be sending the
                        #    message to
                        # 2. Whether it should be "seen" by f0 (via f0's
                        #    wildcard binding)
                        # 3. The actual message
                        msgs = [ (f1, True,  Message('$.This.Fred','dat1')),
                                 (f1, True,  Message('$.This.Fred','dat2')),
                                 (f1, False, Message('$.That.Fred','dat3')),
                                 (f1, False, Message('$.That.Fred','dat4')),
                                 (f2, True,  Message('$.This.Jim.One','dat1')),
                                 (f2, True,  Message('$.This.Jim.Two','dat2')),
                                 (f2, False, Message('$.That.Jim','dat3')),
                                 (f2, False, Message('$.That.Jim','dat4')),
                                ]

                        for fd,wild,m in msgs:
                            f0.write(m)

                        for fd,wild,m in msgs:
                            if wild:
                                # This is a message that f3 should see
                                a = f3.read()
                                assert a.equivalent(m)

                            # Who else should see this message?
                            b = fd.read()
                            assert b.equivalent(m)

    def test_wildcard_listening_2(self):
        """Test using wildcards to listen - 2, percent.
        """
        with RecordingInterface(0,'rw',self.bindings) as f0:

            with RecordingInterface(0,'r',self.bindings) as f1:
                f1.bind('$.This.Fred')
                f1.bind('$.That.Fred')

                with RecordingInterface(0,'r',self.bindings) as f2:
                    f2.bind('$.This.Jim.One')
                    f2.bind('$.This.Jim.Two')
                    f2.bind('$.That.Jim')

                    with RecordingInterface(0,'r',self.bindings) as f3:
                        f3.bind('$.This.%')

                        # For each tuple, we have:
                        #
                        # 1. The Interface we're meant to be sending the
                        #    message to
                        # 2. Whether it should be "seen" by f0 (via f0's
                        #    wildcard binding)
                        # 3. The actual message
                        msgs = [ (f1, True,  Message('$.This.Fred','dat1')),
                                 (f1, True,  Message('$.This.Fred','dat2')),
                                 (f1, False, Message('$.That.Fred','dat3')),
                                 (f1, False, Message('$.That.Fred','dat4')),
                                 (f2, False, Message('$.This.Jim.One','dat1')),
                                 (f2, False, Message('$.This.Jim.Two','dat2')),
                                 (f2, False, Message('$.That.Jim','dat3')),
                                 (f2, False, Message('$.That.Jim','dat4')),
                                ]

                        for fd,wild,m in msgs:
                            f0.write(m)

                        for fd,wild,m in msgs:
                            if wild:
                                # This is a message that f3 should see
                                a = f3.read()
                                assert a.equivalent(m)

                            # Who else should see this message?
                            b = fd.read()
                            assert b.equivalent(m)

# vim: set tabstop=8 shiftwidth=4 expandtab:
