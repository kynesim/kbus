/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the KBUS Lightweight Linux-kernel mediated
 * message system
 *
 * The Initial Developer of the Original Code is Kynesim, Cambridge UK.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Kynesim, Cambridge UK
 *   Gareth Bailey <gb@kynesim.co.uk>
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU Public License version 2 (the "GPL"), in which case the provisions of
 * the GPL are applicable instead of the above.  If you wish to allow the use
 * of your version of this file only under the terms of the GPL and not to
 * allow others to use your version of this file under the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file under either
 * the MPL or the GPL. 
 *
 * ***** END LICENSE BLOCK *****
 */

#include <stdio.h>
#include "kbus.h"

#define DEBUG 0

ksock kbus_ksock_open(const char *fname, int flags) 
{
  int mask  = O_RDONLY | O_WRONLY | O_RDWR;
  flags = flags & mask;
  
  return open(fname, flags);
}

int kbus_ksock_close(ksock ks) 
{
  return close(ks);
}

int kbus_ksock_bind(ksock ks, const char *name) 
{
  struct  kbus_bind_request kbs;
  int rv;

  memset(&kbs, 0, sizeof(kbs));
  kbs.name = (char *) name;
  kbs.name_len = strlen(name);

  rv = ioctl(ks, KBUS_IOC_BIND, &kbs);
  
  return rv;
}


int kbus_ksock_id(ksock ks, uint32_t *ksock_id) 
{
  int rv;
  rv = ioctl(ks, KBUS_IOC_KSOCKID, ksock_id);

  return rv;
}

int kbus_ksock_next_msg(ksock ks, uint32_t *len) 
{
  int rv;
  rv = ioctl(ks, KBUS_IOC_NEXTMSG, len);

  return rv;
}

static int check_message_sanity(const struct kbus_message_header *kms)
{
  int rv = 0;
  
  if (kms->start_guard != KBUS_MSG_START_GUARD || 
      kms->end_guard != KBUS_MSG_END_GUARD )
    rv = -1;


  /* Should probably think of more checks but this will do for now... */

  return rv;
}

int kbus_ksock_read_msg(ksock ks, struct kbus_message_header **kms, 
                        size_t len) 
{
  int rv = 0;
  int br = 0;
  char *buf = malloc(len);

  if (!buf)
    goto fail;

  while (len > 0) {
    rv = read(ks, buf + br, len);
#if DEBUG
    printf("attemping to read %d bytes...", len);
    printf("read %d bytes\n", rv);    
#endif

    if (rv > 0) {
      len -= rv;
      br += rv; 
    }

    if (rv < 0 && errno != EAGAIN) 
      goto fail;
      
    if (rv == 0)
      break;
  }



  *kms = (struct kbus_message_header *)buf;

  if (check_message_sanity(*kms)) {

#if DEGUG
    printf("Read a non valid message\n");
#endif

    errno = EBADMSG;
    goto fail;
  }


  return rv;

 fail:
  if (buf)
    free(buf);

  *kms = NULL;

  return -1;
}

int kbus_ksock_read_next_msg(ksock ks, struct kbus_message_header **kms)
{
  int rv;
  uint32_t m_stat;
  rv = kbus_ksock_next_msg(ks, &m_stat);
  
  if (rv < 0) 
    return rv;

  if (m_stat > 0) {
    rv = kbus_ksock_read_msg(ks, kms, m_stat);

#if DEBUG
    kbus_msg_dump(*kms,1);
#endif
  }

  return rv;
}

int kbus_ksock_write_msg(ksock ks, const struct kbus_message_header *kms)
{
  int length = sizeof(*kms);
  int written = 0;
  int rv;
  
  if (check_message_sanity(kms)) {
    errno = EBADMSG;
    return -1;
  }

  /* We don't support sending an "entire" message */
  if (kms->name == NULL) {
    errno = EBADMSG;
    return -1;
  }

  while (written < length) {
    rv = write(ks, kms, length - written);
    
    if (rv > 0)
      written += rv;

    if (rv < 0 && errno != EAGAIN)
      return -1;
  }

  return 0;
}

int kbus_ksock_send(ksock ks, struct kbus_msg_id *msg_id) {
  int rv;
  rv = ioctl(ks, KBUS_IOC_SEND, msg_id);

  return rv;
}

int kbus_ksock_send_msg(ksock ks, const struct kbus_message_header *kms, struct kbus_msg_id *msg_id)
{
  int rv;
  rv = kbus_ksock_write_msg(ks, kms);

  if (rv)
    return rv;

  rv = kbus_ksock_send(ks, msg_id);

  return rv;
}

#define KBUS_BYTE_TO_WORD_LENGTH(x) ((x + 3) / 4)

int kbus_msg_create(struct kbus_message_header **kms, 
		    const char *name, uint32_t name_len, /* bytes  */
		    const void *data, uint32_t data_len, /* bytes */
		    uint32_t flags) 
{
  *kms = NULL;

  struct kbus_message_header *buf;
 
  buf = malloc(sizeof(*buf));

  if (!buf) {
    errno = ENOMEM;
    goto fail;
  }

  memset(buf, 0, sizeof(*buf));

  buf->start_guard = KBUS_MSG_START_GUARD;
  buf->flags    = flags;
  buf->name_len = name_len;
  buf->data_len = data_len;
  buf->name = (char *) name;
  buf->data = (void *) data;
  buf->end_guard = KBUS_MSG_END_GUARD;
  
  *kms = buf;

  /* We don't want to build dodgy messages... */
  assert(!check_message_sanity(buf));

  return 0;

 fail:
  if (buf)
    free(buf);

  return -1;
}

void kbus_msg_destroy(struct kbus_message_header *kms) {
  /* We allocated enough space all in one go so just free */
  free(kms);

  return;
}

void kbus_msg_dump(const struct kbus_message_header *kms, int dump_data) 
{
  int i;
  int j;

  printf("Dumping message: %p\n", kms);

  printf("\tStart Guard:\t%08x\n\tEnd Guard:\t%08x\n",
	 kms->start_guard, 
	 kms->end_guard);

  printf("\tId:\t\t{%02d, %02d}\n", kms->id.network_id, kms->id.serial_num);

  printf("\tin_reply_to:\t{%02d, %02d}\n", 
	 kms->in_reply_to.network_id, 
	 kms->in_reply_to.serial_num);

  
  printf("\tTo:\t\t%d\n", kms->to);
  printf("\tFrom:\t\t%d\n", kms->from);
  printf("\tFlags:\t\t%08x\n", kms->flags);
  printf("\tName length:\t%d\n", kms->name_len);
  printf("\tData length:\t%d\n", kms->data_len);
  printf("\n");
  printf("\tDumping name:\n\t");

  char *name_ptr = kbus_name_ptr((struct kbus_message_header *)kms);
  uint32_t *data_ptr = kbus_data_ptr((struct kbus_message_header *)kms);

  for (i = 0; i < kms->name_len; i ++) {
    if (name_ptr[j] > ' ' && name_ptr[j] < '~')
      printf("%c", name_ptr[j]);
    else 
      printf(".");
  }
  printf("\n");    

  printf("\n\tDumping data:\n\t");

  int data_limit = (kms->data_len / 4);
  for (i = 0; i < data_limit; i ++) {
    printf("%08x ",data_ptr[i]);
  }
  printf("\n\t");

  char *data_cptr = (char *)data_ptr;
  for (i = 0; i < kms->data_len; i ++) {
    if (data_cptr[j] > ' ' && data_cptr[j] < '~')
      printf("%c", data_cptr[j]);
    else 
      printf(".");
  }
  printf("\n");

}



