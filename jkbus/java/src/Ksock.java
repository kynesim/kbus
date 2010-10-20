package com.kynesim.kbus;

import java.util.*;

public class Ksock {
    private int id;


    class KsockException extends Exception {

    }

    
    //Native method declaration
    public native byte[] loadFile(String name);

    //Load the library
    static {
        System.loadLibrary("jkbus");
     }
    

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
        if (mode != "r" && mode != "rw") {
            /* FIXME: Throw correct exception*/
            throw new KsockException();
        }

        
    }

    /**
     * Bind the given name to the file descriptor.
     *
     * @param name the message name that we wish to bind to as a replier. 
     *
     * @param replier if true then we are binding as the only fd that can 
     *        reply to this message name.mode should be 'r' or'rw' - i.e., 
     *        whether to open the device for read or write (opening for 
     *        write also allows reading, of course).
     */
    public void bind(String name, boolean replier) {

    }

    /** 
     * Close the Ksock, no more opperations may be performed.
     */
    public void close() {

    }

    /**
     * Discard the message being written.
     *
     * Indicates that we have should throw away the message we've been 
     * writing. Has no effect if there is no current message being written
     * (for instance, because 'send' has already been called). be sent.
     */
    public void discard() {

    }

    /**
     * Return the integer file descriptor from our internal fd.
     * 
     * This allows a Ksock instance to be used in a call of select.select()
     * - so, for instance, on should be able to do:
     * (r, w, x) = select.select([ksock1, ksock2, ksock3], None, None)
     * instead of the (less friendly, but also valid):
     * (r, w, x) = select.select([ksock1.fd, ksock2.fd, ksock3.fd], None, None)
     */
    public void fileno() {

    }


    /**
     * Return the internal ‘Ksock id’ for this file descriptor.
     */
    public int ksock_id() {
        
        return id;
    }

}