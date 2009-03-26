from __future__ import with_statement

from kbus import Interface, Message, Request, Reply

with Interface(0,'rw') as f:
    name1 = '$.Fred.Jim'
    name2 = '$.Fred.Bob.William'
    name3 = '$.Fred.Bob.Jonathan'

    f.bind(name1,True)     # replier
    f.bind(name1,False)    # and listener
    f.bind(name2,True)     # replier
    f.bind(name3,False)    # listener

    msg1 = Message(name1,data='dat1')
    msg2 = Request(name2,data='dat2')
    msg3 = Request(name3,data='dat3')

    f.write(msg1)
    f.write(msg2)
    f.write(msg3)

    m1 = f.read()
    m2 = f.read()
    m3 = f.read()

    # For message 1, we only see it as a listener
    # (because it is not a Request) so there is no reply needed
    assert not m1.should_reply()

    # For message 2, a reply is wanted, and we are the replier
    assert m2.should_reply()

    # For message 3, a reply is wanted, but we are just a listener
    assert not m3.should_reply()

    # So, we should reply to message 2 - let's do so

    # We can make a reply "by hand"
    (id,in_reply_to,to,from_,flags,name,data_array) = m2.extract()
    reply_by_hand = Message(name, data=None, in_reply_to=id, to=from_)

    # But it is easier to use the pre-packaged mechanism
    reply = Reply(m2)

    print reply_by_hand
    print reply

    # These should, however, give the same result
    assert reply == reply_by_hand

    # And the obvious thing to do with a reply is
    f.write(reply)

    # We should receive that reply, even though we're not
    # a listener for the message (that's the *point* of replies)
    m4 = f.read()
    assert m4.equivalent(reply)

    # And there shouldn't be anything else to read
    assert f.next_len() == 0
