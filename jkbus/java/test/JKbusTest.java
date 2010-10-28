import java.util.*;
import com.kynesim.kbus.*;

class JKbusTest {
    public static void main(String args[]) {
        System.out.printf("Hello, testing jkbus...\n");
        Ksock ks;

        try {
            ks = new Ksock(0, "rw");
            ks.close();
        } catch (KsockException e) {
            System.out.printf("Failed to open KSock" + e);
            return;
        }


        byte[] data = {1, 2, 3, 4, 5};

        String msg_name = "$.foo.bar.me";

        /*
        com.kynesim.kbus.Message msg =
            new com.kynesim.kbus.Request(msg_name, data,  Message.FLAG_NONE);

        try {
            MessageId mid = ks.send(msg);
            System.out.printf("MessageId " + mid + "\n");
        } catch (Exception e) {
            System.out.printf("Failed to send :-( \n");
        }
        */
        
        try {
            ks.bind("$.foo.bar.me2", true);
        } catch (Exception e){
            System.out.printf("Failed to bind\n");       
        }

        try {
            System.out.printf("Waiting...\n");
            ks.wait_for_message(Ksock.KBUS_SOCK_READABLE);
            Message kmsg = ks.read_next_message();

            
            byte[] data2 = {3, 4, 5, 6};
            Reply rply = new Reply(kmsg, data2, Message.FLAG_NONE);

            System.out.printf("Done...\n" + kmsg + "\n");
        } catch (Exception e){
            System.out.printf("Failed to wait/retrive message ...\n");
        }


        try {
            ks.unbind("$.foo.bar.me2", true);
        } catch (Exception e){
            System.out.printf("Failed to unbind\n");
        }
        
    }
}
