from __future__ import with_statement

import fcntl, array
from kbus import Interface, Message, Request, Reply

fd = open('/dev/kbus0','r+b',1)
assert fd != None

try:

    from kbus import KbusBindStruct

    name = '$.Fred'
    arg = KbusBindStruct(0,0,len(name),name)
    fcntl.ioctl(fd, Interface.KBUS_IOC_BIND, arg)

    m = Message(name,'data')
    m.array.tofile(fd)

    # We wish to be able to read it back in pieces
    l = fcntl.ioctl(fd, Interface.KBUS_IOC_NEXTLEN, 0)
    print 'Length:',l
    # Let's go for the worst possible case - byte by byte
    data = ''
    for ii in range(l):
	c = fd.read(1)
	assert c != ''
	assert len(c) == 1
	data += c

    r = Message(data)
    assert r.equivalent(m)

finally:
    fd.close()
