package com.kynesim.kbus;


public class KRequest extends com.kynesim.kbus.KMessage {

    public KRequest(String name, byte []data, long flags) {
        
        super(name, data, flags);

        this.flags |= FLAG_WANT_A_REPLY;

    }

}