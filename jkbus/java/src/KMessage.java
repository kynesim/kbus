package com.kynesim.kbus;

import java.util.*;

public class KMessage {
    protected String name;
    protected byte []data;
    protected long flags;

    protected KMessageId id;
    protected KMessageId inReplyTo;
    protected long to;
    protected long from;
    protected KOriginallyFrom origFrom;
    protected KOriginallyFrom finalTo;
    

    

    public static final long FLAG_NONE              = (0);
    public static final long FLAG_WANT_A_REPLY      = (1 << 0);
    public static final long FLAG_WANT_YOU_TO_REPLY = (1 << 1);
    public static final long FLAG_SYNTHETIC         = (1 << 2);
    public static final long FLAG_URGENT            = (1 << 3);
    public static final long FLAG_ALL_OR_WAIT       = (1 << 8);
    public static final long FLAG_ALL_OR_FAIL       = (1 << 9);


    public KMessage(String name, byte []data, long flags /*grr, java no unsigned, grr...*/)
    {
        this.name  = name;
        this.data  = data;
        this.flags = flags;
    }
    
    

    public KMessage(String name, byte []data, long flags, KMessageId id, 
                   KMessageId inReplyTo, long to, long from, 
                   KOriginallyFrom origFrom, KOriginallyFrom finalTo)
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

    public String getName() 
    {
        return name; 
    }

    public byte[] getData() { return data; }
    public long getFlags() { return flags; }

    public KMessageId getId() { return id; }
    public KMessageId getInReplyTo() { return inReplyTo; }
    public long getTo() { return to; }
    public long getFrom() { return from; }
    public KOriginallyFrom getOriginallyFrom() { return origFrom; }
    public KOriginallyFrom getFinallyTo() { return finalTo; }


    public String toString()
    {
        StringBuffer sb = new StringBuffer();
        sb.append("KMessage{name = "); sb.append(name);
        sb.append(", #data ="); sb.append(data.length);
        sb.append(", flags= 0x"); sb.append(Long.toString(flags, 16));
        sb.append(", id="); sb.append(id);
        sb.append(", inReplyTo="); sb.append(inReplyTo);
        sb.append(", to="); sb.append(to);
        sb.append(", from="); sb.append(from);
        sb.append(", originally-from="); sb.append(origFrom);
        sb.append(", finalTo="); sb.append(finalTo);
        sb.append("}");
        return sb.toString();
    }
}
