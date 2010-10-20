import java.util.*;
import com.kynesim.kbus.*;

class KbusTest {
    public static void main(String args[]) {
        System.out.printf("woo\n");

        byte buf[];
        //Create class instance
        Kbus mappedFile=new Kbus();
        //Call native method to load ReadFile.java
        buf=mappedFile.loadFile("ReadFile.java");
        //Print contents of ReadFile.java
        for(int i=0;i<buf.length;i++) {
            System.out.print((char)buf[i]);
        }
    }
}