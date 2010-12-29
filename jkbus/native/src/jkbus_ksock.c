#include <jni.h>

#include <stdio.h>

#include "kbus.h"
#include <sys/poll.h>

#include "com_kynesim_kbus_Ksock.h"

jobject new_orig_from(JNIEnv *env, struct kbus_orig_from *from) {
    jobject jfrom;

    jclass OrigFrom = (*env)->FindClass(env, "com/kynesim/kbus/KOriginallyFrom");
    jmethodID of_c_mid = (*env)->GetMethodID(env, OrigFrom, "<init>", 
                                             "(JJ)V");

    if (OrigFrom == NULL || of_c_mid == NULL){
        return NULL;
    }

    jfrom = (*env)->NewObject(env, OrigFrom, of_c_mid, 
                              (int64_t)from->network_id,
                              (int64_t)from->local_id);

    return jfrom;
}

jobject new_message_id(JNIEnv *env, struct kbus_msg_id *id) {
    jobject jid;

    jclass MessageId = (*env)->FindClass(env, "com/kynesim/kbus/KMessageId");
    jmethodID mid_c_mid = (*env)->GetMethodID(env, MessageId, "<init>", 
                                             "(JJ)V");
    if (MessageId == NULL || mid_c_mid == NULL){
        return NULL;
    }

    jid = (*env)->NewObject(env, MessageId, mid_c_mid, 
                            (int64_t)id->network_id,
                            (int64_t)id->serial_num);

    return jid;
    
}

#define MESSAGE_CONSTRUCTOR_SIGNATURE "(Ljava/lang/String;[BJLcom/kynesim/kbus/KMessageId;Lcom/kynesim/kbus/KMessageId;JJLcom/kynesim/kbus/KOriginallyFrom;Lcom/kynesim/kbus/KOriginallyFrom;)V"
jobject msg_to_jmsg(JNIEnv *env, kbus_message_t *msg) {
    jobject jmsg;
    jobject orig_from;
    jobject final_to;
    jobject id;
    jobject in_reply_to;

    jclass Message = (*env)->FindClass(env, "com/kynesim/kbus/KMessage");
    jmethodID msg_c_mid   = (*env)->GetMethodID(env, Message, "<init>", 
                                                MESSAGE_CONSTRUCTOR_SIGNATURE);
    jstring name;
    jbyteArray data;
    
    if (Message   == NULL || msg_c_mid == NULL) {
        /* class not found exception raised. */
        goto fail;
    }

    name = (*env)->NewStringUTF(env, kbus_msg_name_ptr(msg));
        
    if (name == NULL) {
        goto fail;
    }

    data = (*env)->NewByteArray(env, msg->data_len);
    
    if (name == NULL) {
        goto fail;
    }
    
    (*env)->SetByteArrayRegion(env, data, 0, msg->data_len, 
                               (jbyte *)kbus_msg_data_ptr(msg));
       
    id = new_message_id(env, &(msg->id));
    if (id == NULL) {
        goto fail;
    }

    in_reply_to = new_message_id(env, &(msg->in_reply_to));
    if (in_reply_to == NULL) {
        goto fail;
    }

    orig_from = new_orig_from(env, &(msg->orig_from));
    if (orig_from == NULL) {
        goto fail;
    }

    final_to = new_orig_from(env, &(msg->final_to));
    if (final_to == NULL) {
        goto fail;
    }


    jmsg = (*env)->NewObject(env, Message, msg_c_mid, name, data, (int64_t)msg->flags, id, in_reply_to,  (int64_t)msg->to, (int64_t)msg->from, orig_from, final_to);
    
    return jmsg;
    
 fail:
    return NULL;
}

#if 0
kbus_message_t *jmsg_to_msg(JNIEnv *env, jobject jmsg) {

    jclass      Message = (*env)->GetObjectClass(env, jmsg);
    jfieldID    msg_data_fid;
    jfieldID    msg_name_fid;
    jfieldID    msg_flags_fid;

    /* Look for the instance field s in cls */
    msg_data_fid  = (*env)->GetFieldID(env, Message, "data", "[B");
    msg_flags_fid = (*env)->GetFieldID(env, Message, "flags", "J");
    msg_name_fid  = (*env)->GetFieldID(env, Message, 
                                       "name", "Ljava/lang/String;");

    if (msg_data_fid == NULL || msg_name_fid == NULL || msg_flags_fid == NULL) {
        /* uh, a field was not found, eck. */
        jclass Exception = (*env)->FindClass(env, "java/lang/Exception");
        (*env)->ThrowNew(env, Exception, "No data field in Message class");
        return NULL;
    }

    /* Get the length of the data array and a pointer to the array
     * itself.
     */
    data = (*env)->GetObjectField(env, message, msg_data_fid);   
    data_len = (*env)->GetArrayLength(env, data);
    data_array = (*env)->GetByteArrayElements(env, data, NULL);

    /* Get the name and flags fields */
    message_name = (*env)->GetObjectField(env, message, msg_name_fid);   
    flags        = (*env)->GetLongField(env, message, msg_flags_fid);   
    
    if (data_array == NULL) {
        /* java outofmemeory exception already thrown. return control 
         * to java 
         */
        return NULL;
    }

    msg_name_c_str = (*env)->GetStringUTFChars(env, message_name, NULL);
    if (msg_name_c_str == NULL) {
        /* Out of memory, */
        goto fail_and_release;
    }

}
#endif

JNIEXPORT jint JNICALL Java_com_kynesim_kbus_Ksock_native_1open
                          (JNIEnv *env, jobject jobj, jint dev_no, jint flags) {
    int kflags = 0;

    /* FIXME: do this by accessing static fields.*/
    if (flags == 0) {
        kflags = O_RDONLY;
    } else {
        kflags = O_RDWR;
    }
    
    kbus_ksock_t ks = kbus_ksock_open(dev_no, kflags);

    return ks;
}


JNIEXPORT jint JNICALL Java_com_kynesim_kbus_Ksock_native_1close
(JNIEnv *env, jobject jobj, jint ksock) {
    return kbus_ksock_close(ksock);
}


static void dump_buf(int8_t *buf, int len) {
    int i;

    for (i = 0; i < len; i++) {
        if (i && !(i % 16)) printf("\n");
        printf("%02x ", buf[i]);
    }
    printf("\n");
    
}

JNIEXPORT jobject JNICALL Java_com_kynesim_kbus_Ksock_native_1send_1msg
(JNIEnv *env, jobject jobj, jint ksock, jobject message) {


    jclass      Message = (*env)->GetObjectClass(env, message);
    jfieldID    msg_data_fid;
    jfieldID    msg_name_fid;
    jfieldID    msg_flags_fid;

    jlong       flags;
    jstring     message_name;
    jbyteArray  data;
    jint        data_len;
    jbyte      *data_array;
    jobject     message_id = NULL;
    const char *msg_name_c_str;


    /* Look for the instance field s in cls */
    msg_data_fid  = (*env)->GetFieldID(env, Message, "data", "[B");
    msg_flags_fid = (*env)->GetFieldID(env, Message, "flags", "J");
    msg_name_fid  = (*env)->GetFieldID(env, Message, 
                                       "name", "Ljava/lang/String;");

    if (msg_data_fid == NULL || msg_name_fid == NULL || msg_flags_fid == NULL) {
        /* uh, a field was not found, eck. */
        jclass Exception = (*env)->FindClass(env, "java/lang/Exception");
        (*env)->ThrowNew(env, Exception, "No data field in Message class");
        return NULL;
    }

    /* Get the length of the data array and a pointer to the array
     * itself.
     */
    data = (*env)->GetObjectField(env, message, msg_data_fid);   
    data_len = (*env)->GetArrayLength(env, data);
    data_array = (*env)->GetByteArrayElements(env, data, NULL);

    /* Get the name and flags fields */
    message_name = (*env)->GetObjectField(env, message, msg_name_fid);   
    flags        = (*env)->GetLongField(env, message, msg_flags_fid);   

    if (data_array == NULL) {
        /* java outofmemeory exception already thrown. return control 
         * to java 
         */
        return NULL;
    }

    msg_name_c_str = (*env)->GetStringUTFChars(env, message_name, NULL);
    if (msg_name_c_str == NULL) {
        /* Out of memory, */
        goto fail_and_release;
    }


    printf("Sending to %s\n", msg_name_c_str);

    /* Now do the sending! */
    {
        kbus_message_t *k_msg;
        kbus_msg_id_t id;
        int rv;

        kbus_msg_create(&k_msg, msg_name_c_str, strlen(msg_name_c_str), 
                        data_array, data_len, flags);

        rv = kbus_ksock_send_msg(ksock, k_msg, &id);

        if (rv < 0) {
            /* Ah, we failed, throw an exception to let java code know. */
#define ERR_MSG_BUF_LEN 64
            char err_msg[ERR_MSG_BUF_LEN]; 
            jclass Exception = (*env)->FindClass(env, "com/kynesim/kbus/KsockException");

            if (Exception == NULL) {
                /* classnotfound exception already thrown */
                goto fail_and_release2;
            }
            
            snprintf(err_msg, ERR_MSG_BUF_LEN, "Failed with: %d (%s)", 
                     rv, strerror(rv));

            (*env)->ThrowNew(env, Exception, err_msg);
            goto fail_and_release2;
        }
        
        /* Alright! Now construct a MessageId object for java. */ 
        {
            jclass MessageId = (*env)->FindClass(env, "com/kynesim/kbus/KMessageId");
            
            if (MessageId == NULL) {
                goto fail_and_release2;
            }
            
            jmethodID msgid_c_mid = (*env)->GetMethodID(env, MessageId, "<init>", "(JJ)V");
            message_id = (*env)->NewObject(env, MessageId, msgid_c_mid,
                                           (int64_t)id.network_id, (int64_t)id.serial_num);
            
            if (message_id == NULL) {
                goto fail_and_release2;
            }

        }
    }

    dump_buf(data_array, 0);

    (*env)->ReleaseByteArrayElements(env, data, data_array, 0);
    return message_id;

 /* If somthing went wrong, skip here to free the array and return */
 fail_and_release2:
    (*env)->ReleaseStringUTFChars(env, message_name, msg_name_c_str);
 fail_and_release:
    (*env)->ReleaseByteArrayElements(env, data, data_array, 0);
    return NULL;
}


JNIEXPORT jint JNICALL Java_com_kynesim_kbus_Ksock_native_1wait_1for_1message
(JNIEnv *env, jobject obj, jint ksock, jint wait_for, jint ms) {
    struct pollfd fds[1];
    int rv;

    fds[0].fd = (int)ksock;
    fds[0].events = ((wait_for & KBUS_KSOCK_READABLE) ? POLLIN : 0) |
        ((wait_for & KBUS_KSOCK_WRITABLE) ? POLLOUT : 0);
    fds[0].revents = 0;
    rv = poll(fds, 1, ms);
    if (rv < 0)
    {
        return -errno;
    }
    else
    {
        return ((fds[0].revents & POLLIN) ? KBUS_KSOCK_READABLE : 0) | 
            ((fds[0].revents & POLLOUT) ? KBUS_KSOCK_WRITABLE : 0);
    }
}
        

JNIEXPORT jint JNICALL Java_com_kynesim_kbus_Ksock_native_1bind
(JNIEnv *env, jobject obj, jint ksock, jstring name, jlong is_replier) {
    int rv;
    const char *name_c_str = (*env)->GetStringUTFChars(env, name, NULL);

    if (name_c_str == NULL) {
        /* Out of memory, */
        return -1;
    }

    rv = kbus_ksock_bind(ksock, name_c_str, is_replier);
    (*env)->ReleaseStringUTFChars(env, name, name_c_str);

    return rv;
}

JNIEXPORT jint JNICALL Java_com_kynesim_kbus_Ksock_native_1unbind
(JNIEnv *env, jobject obj, jint ksock, jstring name, jlong is_replier) {
    int rv;
    const char *name_c_str = (*env)->GetStringUTFChars(env, name, NULL);

    if (name_c_str == NULL) {
        /* Out of memory, */
        return -1;
    }

    rv = kbus_ksock_unbind(ksock, name_c_str, is_replier);
    (*env)->ReleaseStringUTFChars(env, name, name_c_str);

    return rv;
}

JNIEXPORT jobject JNICALL Java_com_kynesim_kbus_Ksock_native_1read_1next_1message
(JNIEnv *env, jobject obj, jint ksock)
{
    kbus_message_t *msg;
    int rv;
    jobject jmsg = NULL;


    rv = kbus_ksock_read_next_msg(ksock, &msg);

    if (rv < 0) {
        /* somthing went wrong */
        return NULL;
    }

    jmsg = msg_to_jmsg(env, msg);
        
    if (jmsg == NULL) {
        goto fail_and_release;
    }


    return jmsg;

 fail_and_release:
    kbus_msg_delete(&msg);
    return NULL;
   
}
