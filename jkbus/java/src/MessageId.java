package com.kynesim.kbus;

public class MessageId {
    /* Curse the lack of unsigned int */
    private long network_id;
	private long serial_num;

    public MessageId(long network_id, long serial_num) {
        System.out.printf("in constructor %d %d \n", network_id, serial_num);
        this.network_id = network_id;
        this.serial_num = serial_num;
    }

    public long getNetworkId() {
        return network_id;
    }


    public long getSerialNum() {
        return serial_num;
    }

    public String toString() {
        return "MessageId(network_id: " + network_id + ", serial_num: " + serial_num + ")";
    }
}