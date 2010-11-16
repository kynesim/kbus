package com.kynesim.kbus;

import java.util.*;

public class Message {
    protected String name;
    protected byte []data;
    protected long flags;

    protected MessageId id;
    protected MessageId inReplyTo;
    protected long to;
    protected long from;
    protected OriginallyFrom origFrom;
    protected OriginallyFrom finalTo;
    

    

    public static final long FLAG_NONE              = (0);
    public static final long FLAG_WANT_A_REPLY      = (1 << 0);
    public static final long FLAG_WANT_YOU_TO_REPLY = (1 << 1);
    public static final long FLAG_SYNTHETIC         = (1 << 2);
    public static final long FLAG_URGENT            = (1 << 3);
    public static final long FLAG_ALL_OR_WAIT       = (1 << 8);
    public static final long FLAG_ALL_OR_FAIL       = (1 << 9);


    public Message(String name, byte []data, long flags /*grr, java no unsigned, grr...*/)
    {
        this.name  = name;
        this.data  = data;
        this.flags = flags;
    }
    
    

    public Message(String name, byte []data, long flags, MessageId id, 
                   MessageId inReplyTo, long to, long from, 
                   OriginallyFrom origFrom, OriginallyFrom finalTo)
    {
        this.name      = name;
        this.data      = data;
        this.flags     = flags;
        this.id        = id;
        this.inReplyTo = inReplyTo;
        this.to        = to;
        this.from      = from;
        this.origFrom  = origFrom;
        this.finalTo   = finalTo;
    }

    public boolean wantsUsToReply() {
        return ((flags & FLAG_WANT_A_REPLY) != 0) &&
            ((flags & FLAG_WANT_YOU_TO_REPLY) != 0);
    }

    public boolean isRequest() {
        return ((flags & FLAG_WANT_A_REPLY) != 0);
    }

    public boolean isReply() {
        return ((inReplyTo.getNetworkId()) != 0) ||
            ((inReplyTo.getSerialNum()) != 0);
    }


}
