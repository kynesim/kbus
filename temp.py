import os
from kbus import Message, bind, next_len
from array import array

print 'Opening device f for read/write'
f = open('/dev/kbus0','wb+')

name1 = '$.Fred.Jim'
name2 = '$.Fred.Bob.William'
name3 = '$.Fred.Bob.Jonathan'

bind(f,name1,True)     # replier
bind(f,name2,True)     # replier
bind(f,name3,False)    # just listener

msg1 = Message(name1,data='dat1')
msg1.to_file(f)
m1 = Message(f.read(next_len(f)))
# For message 1, there is no reply needed
assert not m1.should_reply()

msg2 = Message(name2,data='dat2',flags=Message.WANT_A_REPLY)
msg2.to_file(f)
m2 = Message(f.read(next_len(f)))
# For message 2, a reply is wanted, and we are the replier
print m1
assert m2.should_reply()

msg3 = Message(name3,data='dat3',flags=Message.WANT_A_REPLY)
msg3.to_file(f)
m3 = Message(f.read(next_len(f)))
# For message 3, a reply is wanted, and we are not the replier
assert not m1.should_reply()



# So, we should reply to message 2 - let's do so
(id,in_reply_to,to,from_,flags,name,data_array) = msg2.extract()

reply = Message(name, data=None, in_reply_to=id, to=from_)
reply.to_file(f)

# And we should be able to read it...
m4 = Message(f.read(next_len(f)))

assert m4.equivalent(reply)

f.close()
