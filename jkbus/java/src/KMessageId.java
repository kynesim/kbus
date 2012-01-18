package com.kynesim.kbus;

public class KMessageId {
    /* Curse the lack of unsigned int */
    private long networkId;
    private long serialNum;

    public KMessageId(long networkId, long serialNum) {
        // System.out.printf("in constructor %d %d \n", networkId, serialNum);
        this.networkId = networkId;
        this.serialNum = serialNum;
    }

    public long getNetworkId() {
        return networkId;
    }

    public long getSerialNum() {
        return serialNum;
    }

    public String toString() {
        return "KMessageId{network_id: " + networkId + ", serial_num: " + serialNum + "}";
    }

    public boolean equals(Object other)
    {
        if (!(other instanceof KMessageId)) { return false ; }
        KMessageId msg = (KMessageId) other;
        return this.networkId == msg.networkId && this.serialNum == msg.serialNum;
    }
    
}
