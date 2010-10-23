package com.kynesim.kbus;

import java.util.*;

public class Message {
    private String name;
    private byte []data;
    private long flags;

    private MessageId id;
    private MessageId in_reply_to;
    private long to;
    private long from;
    private OriginallyFrom orig_from;
    private OriginallyFrom final_to;
    

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
                   MessageId in_reply_to, long to, long from, 
                   OriginallyFrom orig_from, OriginallyFrom final_to)
    {
        this.name        = name;
        this.data        = data;
        this.flags       = flags;
        this.id          = id;
        this.in_reply_to = in_reply_to;
        this.to          = to;
        this.from        = from; 
        this.orig_from   = orig_from;
        this.final_to    = final_to;
    }

    public String toString() {
        String out = "Msg{ Name: " + name + ", data: ";

        try {
            out += new String(data, "US-ASCII");
        } catch (Exception e) {
            /*shrug*/
        }

        out += " }\n";

        return out;
    }

    


}