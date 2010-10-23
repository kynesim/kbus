package com.kynesim.kbus;


public class Reply extends com.kynesim.kbus.Message {

    public Reply(Message msg, byte []data, long flags) throws KsockException {
        
        super(msg.name, data, flags);

        if (!msg.wantsUsToReply()) {
            throw new KsockException("This Message doesn't want a reply.");
        }

        this.to          = msg.from;
        this.in_reply_to = msg.id;

    }

}