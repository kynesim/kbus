package com.kynesim.kbus;

import java.util.*;

public class Ksock {
    private int ksockFd;

    /* ---- NATIVE ---- */

    static {
        System.loadLibrary("jkbus");
    }

    private native int native_open(int device_number, int flags);
    private native int native_close(int ksock);

    private native int native_wait_for_message(int ksock, int waitfor);
 
    private native  com.kynesim.kbus.MessageId native_send_msg(int     ksock,
                                                               com.kynesim.kbus.Message  msg) throws KsockException;

    
    private native int native_bind(int ksock,  String name, long isReplier);
    private native int native_unbind(int ksock,  String name, long isReplier);
    private native com.kynesim.kbus.Message native_read_next_message(int ksock);

    /* ---- CONSTANTS ---- */

    public static final int KBUS_SOCK_READABLE  = (1 << 0);
    public static final int KBUS_SOCK_WRITEABLE = (1 << 1);



    /* ---- THERESTTM ---- */    

    /**
     * Constructor.
     *
     * @param which which KBUS device to open - so if 'which' is 3, 
     *        we open /dev/kbus3.  the text of the tool tip.
     *
     * @param mode should be 'r' or'rw' - i.e., whether to open the device for
     *        read or write (opening for write also allows reading, of course).
     */
    public Ksock(int which, String mode) throws KsockException {
        int flags = 0;

        if (mode != "r" && mode != "rw") {
            /* FIXME: Throw correct exception*/
            throw new KsockException();
        }

        /* quick and dirty, 0 for read 1 for read/write. */
        if (mode == "r") {
            flags = 0;
        } else {
            flags = 1;

        }
        
        ksockFd = native_open(which, flags);
        
        System.out.printf("foo %d\n", ksockFd);

        if (ksockFd < 0) {
            throw  new KsockException();
        }                
    }



    /** 
     * Close the Ksock, no more opperations may be performed.
     */
    public void close() {
        native_close(ksockFd);
    }

    /**
     * Return the internal ‘Ksock id’ for this file descriptor.
     */
    public int getKsockFd() {        
        return ksockFd;
    }


    /**
     * Send a message.
     *
     * @param message the message to send.
     *
     * @return message id of the message just sent.
     */
    public MessageId send(Message message) throws KsockException {
        MessageId mid = null;
        
        try {
            mid = native_send_msg(ksockFd, message);
        } catch (KsockException e) {
            System.out.printf("Failed While Sending: Exception " + e + "\n");
            throw e;
        }


        return mid;
    }

    /**
     * Wait until either the Ksock may be read from or written to.
     *
     * Returns when there is data to be read from the Ksock, or the Ksock
     * may be written to.
     *
     * @param waitFor indicates what to wait for. It should be set to
     * ``KBUS_SOCK_READABLE``, ``KBUS_SOCK_WRITABLE``, or the two "or"ed together,
     * as appropriate.
     *
     * @return ``KBUS_SOCK_READABLE``, ``KBUS_SOCK_WRITABLE``, or the two "or"ed
     * together to indicate which operation is ready, or a negative number
     * (``-errno``) for failure.
     */
    public int waitForMessage(int waitFor) throws KsockException{
        int rv = native_wait_for_message(ksockFd, waitFor);

        if (rv < 0) {
            throw new KsockException("Waiting failed. (retval: " + rv + ")");
        }

        return rv;
    }


    /**
     *  Bind the given name to the ksock.
     *
     * @param name a string containing the name to bind to.
     *
     * @param is_replier if true then we are binding as the only ksock that can 
     *        reply to this message name.
     */

    public void bind(String name, boolean isReplier) throws KsockException {
        
        int rv = native_bind(ksockFd, name, (isReplier)? 1: 0);

        if (rv < 0) {
            throw new KsockException("Failed to bind (retval: " + rv + ")");
        }

        return;
    }

    /**
     * Unbind the given name from the file descriptor.
     *
     * The arguments need to match the binding that we want to unbind.
     *
     */
    public void unbind(String name, boolean isReplier) throws KsockException {
        
        int rv = native_unbind(ksockFd, name, (isReplier)? 1: 0);

        if (rv < 0) {
            throw new KsockException("Failed to bind (retval: " + rv + ")");
        }

        return;
    }

    /**
     * Read the next Message.
     *
     * Throws an excaption if no message is present to be read. 
     */

    public com.kynesim.kbus.Message readNextMessage() throws KsockException {
        Message m = native_read_next_message(ksockFd);

        if (m == null) {
            throw new KsockException("No message recived.");
        }

        return m;
    }



}
