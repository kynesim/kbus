package com.kynesim.kbus;


public class Request extends com.kynesim.kbus.Message {

    public Request(String name, byte []data, long flags) {
        
        super(name, data, flags);

        this.flags |= FLAG_WANT_A_REPLY;

    }

}