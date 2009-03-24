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

    (note this ignores the zeroth element of that tuple, which is a message
    id), or from an equivalent list::

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
        return self.array == other.array

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

    def extract(self):
        """Return our parts as a tuple.

        The values are returned in something approximating the order
        within the message itself:

            (id,in_reply_to,to,from_,flags,name,data_array)

        This is not the same order as arguments to Message().
        """

        # Sanity check:
        assert self.array[self.IDX_START_GUARD] == self.START_GUARD
        assert self.array[self.IDX_END_GUARD] == self.END_GUARD

        msg = self.array
        id = msg[self.IDX_ID]
        in_reply_to = msg[self.IDX_IN_REPLY_TO]
        to = msg[self.IDX_TO]
        from_ = msg[self.IDX_FROM]
        flags = msg[self.IDX_FLAGS]
        name_len = msg[self.IDX_NAME_LEN]
        data_len = msg[self.IDX_DATA_LEN]
        name_array_len = (name_len+3)/4

        base = self.IDX_DATA_LEN + 1

        name_array = msg[base:base+name_array_len]
        name = name_array.tostring()
        # Note that if the message was well constructed, any padding bytes
        # at the end of the name will be '\0', and thus not show when printed
        #print '%d<%s>'%(len(name),name),
        # Make sure we remove the padding bytes
        name = name[:name_len]

        data_offset = base+name_array_len
        data_array = msg[data_offset:data_offset+data_len]
        #print '<%s>'%(data_array.tostring())

        return (id,in_reply_to,to,from_,flags,name,data_array)

    def to_file(self,f):
        """Write the Message's data to a file.

        'f' is the file object to write to (expected to be an instance of
        opening '/dev/kbus0').

        NB: flushes the output when it's done.
        """
        self.array.tofile(f)
        f.flush()

        return self

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
    """A wrapper around a KBUS device, for pusposes of message sending.

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
            return '<KBUS Interface %s open for %s>'%(self.name,self.mode)
        else:
            return '<KBUS Interface %s closed>'%(self.name)

    def close(self):
        ret = self.fd.close()
        self.fd = None
        self.mode = None
        return ret

    def bind(self,name,replier=True,guaranteed=False):
        """Bind the given name to the file descriptor.

        If 'replier', then we are binding as the only fd that can reply to this
        message name.

            XXX Is 'True' actually a sensible default for 'replier'? Normally one
            XXX *does* want a single replier, but I've found myself calling 'bind'
            XXX multiple times with the same message name, and forgetting that I
            XXX need to say the listeners are not repliers. Is there a better
            XXX (separate) error code that the ioctl could return in this case?

        If 'guaranteed', then we require that *all* messages to us be delivered,
        otherwise kbus may drop messages if necessary.
        """
        arg = KbufBindStruct(replier,guaranteed,len(name),name)
        return fcntl.ioctl(self.fd, KBUS_IOC_BIND, arg);

    def unbind(self,name,replier=True,guaranteed=False):
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
        assert False, 'Applying %s did not fail with IOError'%stuff
    except IOError, e:
        actual_errno = e.args[0]
        errno_name = errno.errorcode[actual_errno]
        expected_errno_name = errno.errorcode[expected_errno]
        assert actual_errno == expected_errno, \
                'expected %s, got %s'%(expected_errno_name,errno_name)
    except Exception, e:
        print e
        assert False, 'Applying %s failed with %s, not IOError'%(stuff,sys.exc_type)

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

    # A dictionary linking open /dev/kbus0 instances to replier True/False
    # and guaranteed-delivery True/False flags, and message names - so, for
    # instance:
    #
    #    bindings[f] = [(True,False,'$.Fred.Jim.Bob'), (False,True,'$.Fred')]
    #
    # (the order in the tuple matches that in the /proc/kbus/bindings file).
    #
    # Automatically managed by the local bind and unbind *methods*
    bindings = {}

    def bind(self,f,name,replier=True,guaranteed=False):
        """A wrapper around the 'bind' function. to keep track of bindings.
        """
        f.bind(name,replier,guaranteed)
        TestKernelModule.bindings[f].append( (replier,guaranteed,name) )

    def unbind(self,f,name,replier=True,guaranteed=False):
        """A wrapper around the 'unbind' function, to keep track of bindings.
        """
        f.unbind(name,replier,guaranteed)
        l = TestKernelModule.bindings[f]
        # If there are multiple matches, we'll delete the first,
        # which is what we want (well, to delete a single instance)
        for index,thing in enumerate(l):
            if thing[-1] == name:       # the name is always the last element
                del l[index]
                break
        # No matches shouldn't occur, but let's ignore it anyway

    def attach(self,mode):
        """A wrapper around opening /dev/kbus0, to keep track of bindings.
        """
        f = Interface(0,mode)
        if f:
            TestKernelModule.bindings[f] = []
        return f

    def detach(self,f):
        """A wrapper around closing a /dev/kbus0 instance.
        """
        del TestKernelModule.bindings[f]
        return f.close()

    def _check_bindings(self):
        """Check the bindings we think we have match those of kbus
        """
        expected = []
        for fd,l in TestKernelModule.bindings.items():
            for r,a,n in l:
                expected.append( (fd,r,a,n) )
        assert bindings_match(expected)

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
        f = self.attach('r')
        assert f != None
        try:
            # Nothing to read
            assert f.read() == None

            # We can't write to it
            msg2 = Message('$.Fred','data')
            check_IOError(errno.EBADF,f.write,msg2)
        finally:
            assert self.detach(f) is None

    def test_readwrite_kbus0(self):
        """If we open the device read/write, we can read and write.
        """
        f = self.attach('rw')
        assert f != None

        try:
            self.bind(f,'$.B')
            self.bind(f,'$.C')

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
            assert self.detach(f) is None

    def test_two_opens_kbus0(self):
        """If we open the device multiple times, they communicate
        """
        f1 = self.attach('rw')
        assert f1 != None
        try:
            f2 = self.attach('rw')
            assert f2 != None
            try:
                # Both files listen to both messages
                self.bind(f1,'$.B',False)
                self.bind(f1,'$.C',False)
                self.bind(f2,'$.B',False)
                self.bind(f2,'$.C',False)

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
                assert self.detach(f2) is None
        finally:
            assert self.detach(f1) is None

    def test_bind(self):
        """Initial ioctl/bind test.
        """
        f = self.attach('rw')
        assert f != None

        try:
            # - BIND
            # Low level check: The "Bind" ioctl requires a proper argument
            check_IOError(errno.EINVAL, fcntl.ioctl, f.fd, KBUS_IOC_BIND, 0)
            # Said string must not be zero length
            check_IOError(errno.EINVAL, self.bind, f, '', True)
            # At some point, it will have restrictions on what it *should* look
            # like
            self.bind(f,'$.Fred')
            # - UNBIND
            check_IOError(errno.EINVAL, fcntl.ioctl, f.fd, KBUS_IOC_UNBIND, 0)
            check_IOError(errno.EINVAL, self.unbind, f, '', True)
            self.unbind(f,'$.Fred')
        finally:
            assert self.detach(f) is None

    def test_many_bind_1(self):
        """Initial ioctl/bind test -- make lots of bindings
        """
        f = self.attach('rw')
        assert f != None

        try:
            self.bind(f,'$.Fred')
            self.bind(f,'$.Fred.Jim')
            self.bind(f,'$.Fred.Bob')
            self.bind(f,'$.Fred.Jim.Bob')
        finally:
            assert self.detach(f) is None

    def test_many_bind_2(self):
        """Initial ioctl/bind test -- make lots of the same binding
        """
        f = self.attach('rw')
        assert f != None

        try:
            self.bind(f,'$.Fred')
            self.bind(f,'$.Fred',False)
            self.bind(f,'$.Fred',False)
            self.bind(f,'$.Fred',False)
        finally:
            assert self.detach(f) is None

    def test_many_bind_3(self):
        """Initial ioctl/bind test -- multiple matching bindings/unbindings
        """
        f = self.attach('rw')
        assert f != None

        try:
            self.bind(f,'$.Fred')       # But remember, only one replier
            self.bind(f,'$.Fred',False)
            self.bind(f,'$.Fred',False)
            self.unbind(f,'$.Fred')
            self.unbind(f,'$.Fred',False)
            self.unbind(f,'$.Fred',False)
            # But not too many
            check_IOError(errno.EINVAL, self.unbind,f, '$.Fred')
            check_IOError(errno.EINVAL, self.unbind,f, '$.Fred',False)
            # We can't unbind something we've not bound
            check_IOError(errno.EINVAL, self.unbind,f, '$.JimBob',False)
        finally:
            assert self.detach(f) is None

    def test_bind_more(self):
        """Initial ioctl/bind test - with more bindings.
        """
        f1 = self.attach('rw')
        assert f1 != None
        try:
            f2 = self.attach('rw')
            assert f2 != None
            try:
                # We can bind and unbind
                self.bind(f1,'$.Fred',replier=True)
                self.unbind(f1, '$.Fred',replier=True)
                self.bind(f1,'$.Fred',replier=False)
                self.unbind(f1, '$.Fred',replier=False)
                # We can bind many times
                self.bind(f1,'$.Fred',replier=False)
                self.bind(f1,'$.Fred',replier=False)
                self.bind(f1,'$.Fred',replier=False)
                # But we can only have one replier
                self.bind(f1,'$.Fred',replier=True)
                check_IOError(errno.EADDRINUSE, self.bind,f1, '$.Fred',True)

                # Two files can bind to the same thing
                self.bind(f1,'$.Jim.Bob',replier=False)
                self.bind(f2,'$.Jim.Bob',replier=False)
                # But we can still only have one replier
                # (the default is to bind a replier, since we expect that in
                # general there should be one, and if the binder is *not* a
                # replier, they probably should have thought about this).
                self.bind(f1,'$.Jim.Bob')
                check_IOError(errno.EADDRINUSE, self.bind,f2, '$.Jim.Bob')

                # Oh, and not all messages need to be received
                # - in our interfaces, we default to allowing kbus to drop
                # messages if necessary
                self.bind(f1,'$.Jim.Bob',replier=False,guaranteed=True)
                self.bind(f1,'$.Fred',replier=False,guaranteed=True)
            finally:
                assert self.detach(f2) is None
        finally:
            assert self.detach(f1) is None

    def test_bindings_match1(self):
        """Check that bindings match inside and out.
        """
        f1 = self.attach('rw')
        assert f1 != None
        try:
            f2 = self.attach('rw')
            assert f2 != None
            try:
                self.bind(f1,'$.Fred')
                self.bind(f1,'$.Fred.Jim')
                self.bind(f1,'$.Fred.Bob')
                self.bind(f1,'$.Fred.Jim.Bob')
                self.bind(f1,'$.Fred.Jim.Derek',False)
                # /proc/kbus/bindings should reflect all of the above, and none other
                self._check_bindings()
                self.bind(f2,'$.Fred.Jim.Derek',False)
                self.bind(f2,'$.William',False)
                self.bind(f2,'$.William',False)
                self.bind(f2,'$.William',False)
                self.bind(f1,'$.Fred.Jim.Bob.Eric')
                self._check_bindings()
            finally:
                assert self.detach(f2) is None
        finally:
            assert self.detach(f1) is None
        # And now all of the bindings *should* have gone away
        self._check_bindings()

    def test_rw_single_file(self):
        """Test reading and writing two messages on a single file
        """
        f = self.attach('rw')
        assert f != None
        try:

            name1 = '$.Fred.Jim'
            data1 = array.array('L','datadata')

            name2 = '$.Fred.Bob.William'
            data2 = array.array('L','This is surely some data')

            # Bind so that we can write/read the first, but not the second
            self.bind(f,name1)
            self.bind(f,'$.William')

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
            assert self.detach(f) is None

    def test_read_write_2files(self):
        """Test reading and writing between two files.
        """
        f1 = self.attach('rw')
        assert f1 != None
        try:
            f2 = self.attach('rw')
            assert f2 != None
            try:
                self.bind(f1,'$.Fred')
                self.bind(f1,'$.Fred',False)
                self.bind(f1,'$.Fred',False)

                self.bind(f2,'$.Jim')

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
                assert self.detach(f2) is None
        finally:
            assert self.detach(f1) is None

    def test_reply_single_file(self):
        """Test replying with a single file
        """
        f = self.attach('rw')
        assert f != None
        try:

            name1 = '$.Fred.Jim'
            name2 = '$.Fred.Bob.William'
            name3 = '$.Fred.Bob.Jonathan'

            self.bind(f,name1,True)     # replier
            self.bind(f,name2,True)     # replier
            self.bind(f,name3,False)    # just listener


            msg1 = Message(name1,data='dat1')
            msg2 = Message(name2,data='dat2',flags=Message.WANT_A_REPLY)
            msg3 = Message(name3,data='dat3',flags=Message.WANT_A_REPLY)

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
            (id,in_reply_to,to,from_,flags,name,data_array) = msg2.extract()

            reply = Message(name, data=None, in_reply_to=id, to=from_)
            f.write(reply)

            # And we should be able to read it...
            m4 = f.read()
            assert m4.equivalent(reply)

            # And there shouldn't be anything else to read
            assert f.next_len() == 0
        finally:
            assert self.detach(f) is None

# vim: set tabstop=8 shiftwidth=4 expandtab:
