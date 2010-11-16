package com.kynesim.kbus;

public class MessageId {
    /* Curse the lack of unsigned int */
    private long networkId;
    private long serialNum;

    public MessageId(long networkId, long serialNum) {
        System.out.printf("in constructor %d %d \n", networkId, serialNum);
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
        return "MessageId(network_id: " + networkId + ", serial_num: " + serialNum + ")";
    }
}
