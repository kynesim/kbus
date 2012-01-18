package com.kynesim.kbus;

public class KOriginallyFrom {
    public long networkId;
    public long localId;

    public KOriginallyFrom(long nid, long lid) {
        networkId = nid;
        localId   = lid;

    }

    public boolean equals(Object other)
    {
        if (!(other instanceof KOriginallyFrom)) { return false; }
        KOriginallyFrom kof = (KOriginallyFrom)other;
        return kof.networkId == this.networkId && kof.localId == localId;
    }
    
    public String toString()
    {
        return "KOriginallyFrom{nid=" + networkId + ",lid=" + localId + "}";
    }

}
