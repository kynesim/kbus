import os
from kbus import Message, Interface

print 'Opening device f for read/write'
f = Interface(0,'rw')
print f

name1 = '$.Fred.Jim'
name2 = '$.Fred.Bob.William'
name3 = '$.Fred.Bob.Jonathan'

f.bind(name1,True)     # replier
f.bind(name2,True)     # replier
f.bind(name3,False)    # just listener

msg1 = Message(name1,data='dat1')
f.write(msg1)

m1 = f.read()
# For message 1, there is no reply needed
assert not m1.should_reply()

msg2 = Message(name2,data='dat2',flags=Message.WANT_A_REPLY)
f.write(msg2)
m2 = f.read()
# For message 2, a reply is wanted, and we are the replier
print m2
assert m2.should_reply()

msg3 = Message(name3,data='dat3',flags=Message.WANT_A_REPLY)
f.write(msg3)
m3 = f.read()
# For message 3, a reply is wanted, and we are not the replier
assert not m1.should_reply()


# So, we should reply to message 2 - let's do so
(id,in_reply_to,to,from_,flags,name,data_array) = msg2.extract()

reply = Message(name, data=None, in_reply_to=id, to=from_)
f.write(reply)

# And we should be able to read it...
m4 = f.read()

assert m4.equivalent(reply)

f.close()
print f
