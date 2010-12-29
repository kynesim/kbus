package com.kynesim.kbus;


public class KReply extends com.kynesim.kbus.KMessage {

    public KReply(KMessage msg, byte []data, long flags) throws KsockException {
        
        super(msg.name, data, flags);

        if (!msg.wantsUsToReply()) {
            throw new KsockException("This Message doesn't want a reply.");
        }

        this.to        = msg.from;
        this.inReplyTo = msg.id;

    }

}
