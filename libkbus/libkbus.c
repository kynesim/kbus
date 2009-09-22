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
#include <sys/poll.h>
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

int kbus_ksock_bind(ksock ks, const char *name, uint32_t replier)
{
  struct  kbus_bind_request kbs;
  int rv;

  memset(&kbs, 0, sizeof(kbs));
  kbs.name = (char *) name;
  kbs.name_len = strlen(name);
  kbs.is_replier = replier;

  rv = ioctl(ks, KBUS_IOC_BIND, &kbs);
  
  return rv;
}

int kbus_ksock_only_once(ksock ks, uint32_t request)
{
  int           rv;
  uint32_t      array[1];

  switch (request)
  {
  case 0:
  case 1:
  case 0xFFFFFFFF:
    break;
  default:
    return -EINVAL;
  }

  array[0] = request;
  rv = ioctl(ks, KBUS_IOC_MSGONLYONCE, array);
  if (rv)
    return rv;
  else
    return array[0];
}

int kbus_ksock_id(ksock ks, uint32_t *ksock_id) 
{
  int rv;
  rv = ioctl(ks, KBUS_IOC_KSOCKID, ksock_id);

  return rv;
}

int kbus_wait_for_message(ksock ks, int wait_for)
{
  struct pollfd fds[1];
  int rv;

  fds[0].fd = (int)ks;
  fds[0].events = ((wait_for & KBUS_KSOCK_READABLE) ? POLLIN : 0) | 
    ((wait_for & KBUS_KSOCK_WRITABLE) ? POLLOUT : 0);
  fds[0].revents =0;
  rv = poll(fds, 1, -1);
  if (rv < 0)
    {
      return rv;
    }
  else if (rv == 0)
    {
      return 0;
    }
  else 
    {
      return ((fds[0].revents & POLLIN) ? KBUS_KSOCK_READABLE : 0) |
	((fds[0].revents & POLLOUT) ? KBUS_KSOCK_WRITABLE : 0);
    }
}

int kbus_ksock_next_msg(ksock ks, uint32_t *len) 
{
  int rv;
  rv = ioctl(ks, KBUS_IOC_NEXTMSG, len);

  return rv;
}


/* With the advent of pointy messages this seems a bit defunct :-(
 * should probably breath some life into it at some point. - gb
 */
static int check_message_sanity(const kbus_msg_t *kms)
{
  int rv = 0;
  
  if (kms->header.start_guard != KBUS_MSG_START_GUARD || 
      kms->header.end_guard != KBUS_MSG_END_GUARD )
    rv = -1;
  
  if (KBUS_MSG_IS_ENTIRE(kms)) {
    kbus_msg_entire_t *k = (kbus_msg_entire_t *)kms;
    uint32_t *eg = kbus_end_ptr(k);

    if (*eg != KBUS_MSG_END_GUARD) {
      return -1;
    }

  }

  /* Should probably think of more checks but this will do for now... */

  return rv;
}

int kbus_ksock_read_msg(ksock ks, kbus_msg_t **kms, 
                        size_t len) 
{
  int rv = 0;
  int br = 0, nr_read = 0;
  char *buf = malloc(len);

  if (!buf)
    {
      errno = ENOMEM;
      goto fail;
    }
    
  while (len > 0) {
    nr_read = read(ks, buf + br, len);
#if DEBUG
    printf("attemping to read %d bytes...", len);
    printf("read %d bytes\n", nr_read);    
#endif

    if (nr_read > 0) {
      len -= nr_read;
      br += nr_read; 
    } else if (nr_read < 0) {
      if (errno != EAGAIN && errno != EINTR) {
	goto fail;
      }
    } else {

      break;
    }
  }

  *kms = (kbus_msg_t *)buf;

  if (check_message_sanity(*kms)) {

#if DEBUG
    printf("Read a non valid message\n");
#endif

    errno = EBADMSG;
    goto fail;
  }


  return rv;

 fail:
  free(buf);
  *kms = NULL;

  return -1;
}

int kbus_ksock_read_next_msg(ksock ks, kbus_msg_t **kms)
{
  int rv;
  uint32_t m_stat = 0;
  rv = kbus_ksock_next_msg(ks, &m_stat);
  
  if (rv < 0) 
    {
      return rv;
    }  

  if (rv && m_stat > 0) {
    rv = kbus_ksock_read_msg(ks, kms, m_stat);

#if DEBUG
    kbus_msg_dump(*kms,1);
#endif

    /* kbus_ksock_read_msg() returns 0 on success, so ..
     */
    if (!rv) { rv = 1; }

  } else {
    (*kms) = NULL;
    rv = 0;
  }
    
  return rv;
}


static int write_out(int fd, char *data, size_t length ) {
  int written = 0;
  int rv;
  
  while (written < length) {
    rv = write(fd, data + written, length - written);
    
    if (rv > 0)
      written += rv;
    
    if (rv < 0 && errno != EAGAIN)
      return -1;
  }

  return 0;
}

static int kbus_ksock_write_entire_msg(ksock ks, const kbus_msg_entire_t *kms)
{
  /* We don't support sending an "pointy" message */
  if (!KBUS_MSG_IS_ENTIRE(kms)) {
    errno = EBADMSG;
    return -1;
  }

  return write_out(ks, (char *)kms, KBUS_ENTIRE_MSG_LEN(kms->header.name_len,
							kms->header.data_len));
}


static int kbus_ksock_write_pointy_msg(ksock ks, const kbus_msg_t *kms)
{
  /* We don't support sending an "entire" message */
  if (KBUS_MSG_IS_ENTIRE(kms)) {
    errno = EBADMSG;
    return -1;
  }

  return write_out(ks, (char *)kms, sizeof(*kms));
}

int kbus_ksock_write_msg(ksock ks, const kbus_msg_t *kms)
{
  int rv;

  if (check_message_sanity(kms)) {
    errno = EBADMSG;
    return -1;
  }

  if (KBUS_MSG_IS_ENTIRE(kms)) {
    rv = kbus_ksock_write_entire_msg(ks, KBUS_MSG_TO_ENTIRE(kms));
  } else {
    rv = kbus_ksock_write_pointy_msg(ks, kms);
  }

  return rv;
}

int kbus_ksock_send(ksock ks, struct kbus_msg_id *msg_id) {
  int rv;
  rv = ioctl(ks, KBUS_IOC_SEND, msg_id);

  return rv;
}

int kbus_ksock_send_msg(ksock ks, const kbus_msg_t *kms, struct kbus_msg_id *msg_id)
{
  int rv;
  rv = kbus_ksock_write_msg(ks, kms);

  if (rv)
    return rv;

  rv = kbus_ksock_send(ks, msg_id);

  return rv;
}

#define KBUS_BYTE_TO_WORD_LENGTH(x) ((x + 3) / 4)


int kbus_msg_create_entire(kbus_msg_t **kms, 
			   const char *name, uint32_t name_len, /* bytes  */
			   const void *data, uint32_t data_len, /* bytes */
			   uint32_t flags) 
{
  int di = KBUS_ENTIRE_MSG_DATA_INDEX(name_len);
  int eg = KBUS_ENTIRE_MSG_END_GUARD_INDEX(name_len,data_len);
  kbus_msg_entire_t *buf;

  *kms = NULL;
 
  buf    = malloc(KBUS_ENTIRE_MSG_LEN(name_len, data_len));
 
  if (!buf) {
    errno = ENOMEM;
    goto fail;
  }

  memset(buf, 0, KBUS_ENTIRE_MSG_LEN(name_len, data_len));

  buf->header.start_guard = KBUS_MSG_START_GUARD;
  buf->header.flags       = flags;
  buf->header.name_len    = name_len;
  buf->header.data_len    = data_len;
  buf->header.end_guard   = KBUS_MSG_END_GUARD;

  memcpy(&buf->rest[0],  name, name_len);
  memcpy(&buf->rest[di], data, data_len);

  buf->rest[eg] = KBUS_MSG_END_GUARD;

  *kms = KBUS_ENTIRE_TO_MSG(buf);

  /* We don't want to build dodgy messages... */
  assert(!check_message_sanity(*kms));

  return 0;

 fail:
  if (buf)
    free(buf);

  return -1;
}

int kbus_msg_create(kbus_msg_t **kms, 
		    const char *name, uint32_t name_len, /* bytes  */
		    const void *data, uint32_t data_len, /* bytes */
		    uint32_t flags) 
{
  *kms = NULL;

  kbus_msg_t *buf;
 
  buf = malloc(sizeof(*buf));

  if (!buf) {
    errno = ENOMEM;
    goto fail;
  }

  memset(buf, 0, sizeof(*buf));

  buf->header.start_guard = KBUS_MSG_START_GUARD;
  buf->header.flags    = flags;
  buf->header.name_len = name_len;
  buf->header.data_len = data_len;
  buf->header.name = (char *) name;
  buf->header.data = (void *) data;
  buf->header.end_guard = KBUS_MSG_END_GUARD;
  
  *kms = buf;

  /* We don't want to build dodgy messages... */
  assert(!check_message_sanity(buf));

  return 0;

 fail:
  if (buf)
    free(buf);

  return -1;
}

int kbus_msg_create_entire_reply(kbus_msg_t **kms, 
				 const kbus_msg_t *in_reply_to,
				 const void *data, 
				 uint32_t data_len, /* bytes */
				 uint32_t flags)
{
  char* name;
  int rv;

  kbus_msg_name_ptr(in_reply_to, &name);
  rv = kbus_msg_create_entire(kms, name, in_reply_to->header.name_len, 
			      data, data_len, flags);

  if(rv) {
    return -1;
  }

  (*kms)->header.to          = in_reply_to->header.from;
  (*kms)->header.in_reply_to = in_reply_to->header.id;
  return 0;
}

int kbus_msg_create_reply(kbus_msg_t **kms, 
			  const kbus_msg_t *in_reply_to,
			  const void *data, uint32_t data_len, /* bytes */
			  uint32_t flags)
{
  char* name;
  int rv;
  kbus_msg_name_ptr(in_reply_to, &name);
  rv = kbus_msg_create(kms, name, in_reply_to->header.name_len, data, data_len, flags);

  if(rv){
    return -1;
  }

  (*kms)->header.to          = in_reply_to->header.from;
  (*kms)->header.in_reply_to = in_reply_to->header.id;
  return 0;
}

void kbus_msg_dispose(kbus_msg_t **kms_p) {
  if (!kms_p || !*kms_p) { return; }

  kbus_msg_t *kms = (kbus_msg_t *)(*kms_p);

  /* We allocated enough space all in one go so just free */
  free(kms);
  (*kms_p) = NULL;
  
  return;
}

int kbus_msg_name_ptr(const kbus_msg_t *kms, char **name) {

  struct kbus_message_header *khead =  KBUS_MSG_TO_HEADER(kms);
  *name = kbus_name_ptr(khead);

  return 0;
}

int kbus_msg_data_ptr(const kbus_msg_t *kms, void **data) {
  struct kbus_message_header *khead =  KBUS_MSG_TO_HEADER(kms);
  *data = (char *)(kbus_data_ptr(khead));

  return 0;
}

int kbus_msg_sizeof(const kbus_msg_t *kms) {
  int len;

  if (KBUS_MSG_IS_ENTIRE(kms)) {
    len = KBUS_ENTIRE_MSG_LEN(kms->header.data_len, kms->header.name_len);
  } else {
    len = sizeof(*kms);
  }

  return len;
}


void kbus_msg_dump(const kbus_msg_t *kms, int dump_data) 
{
  int i;

  printf("Dumping message: %p\n", kms);

  printf("\tStart Guard:\t%08x\n\tEnd Guard:\t%08x\n",
	 kms->header.start_guard, 
	 kms->header.end_guard);

  printf("\tId:\t\t{%02d, %02d}\n", kms->header.id.network_id, kms->header.id.serial_num);

  printf("\tin_reply_to:\t{%02d, %02d}\n", 
	 kms->header.in_reply_to.network_id, 
	 kms->header.in_reply_to.serial_num);

  
  printf("\tTo:\t\t%d\n", kms->header.to);
  printf("\tFrom:\t\t%d\n", kms->header.from);
  printf("\tFlags:\t\t%08x\n", kms->header.flags);
  printf("\tName length:\t%d\n", kms->header.name_len);
  printf("\tData length:\t%d\n", kms->header.data_len);
  printf("\tName pointer:\t%p\n", kms->header.name);
  printf("\tData pointer:\t%p\n", kms->header.data);
  printf("\n");
  printf("\tDumping name:\n\t");

  char *name_ptr;
  char *data_ptr; 

  kbus_msg_name_ptr(kms, &name_ptr);
  kbus_msg_data_ptr(kms, (void *)&data_ptr);

  for (i = 0; i < kms->header.name_len; i ++) {
    if (name_ptr[i] > ' ' && name_ptr[i] < '~')
      printf("%c", name_ptr[i]);
    else 
      printf(".");
  }
  printf("\n");    

  printf("\n\tDumping data:\n\t");

  printf("\n\tAs uint8_t .. \n\t");
  int data_limit = (kms->header.data_len);
  for (i = 0; i < data_limit; i ++) {
    if (!(i % 16)) {
      printf("\n\t");
    }
    printf("%02x ",data_ptr[i]);
  }
  printf("\n\t");

  printf("\n\tAs text .. \n\t");

  char *data_cptr = (char *)data_ptr;
  for (i = 0; i < kms->header.data_len; i ++) {
    if (data_cptr[i] >= ' ' && data_cptr[i] <= '~')
      printf("%c", data_cptr[i]);
    else 
      printf(".");
  }
  printf("\n");


 

  printf("\n\tDumping whole message (raw uint8_t) .. \n\t");
  uint8_t *dptr = (uint8_t *)kms;
  for (i = 0; i < kbus_msg_sizeof(kms); i ++) {
    if (!(i % 16)) {
      printf("\n\t");
    }
    printf("%02x ", dptr[i]);

  }
  printf("\n");


}



/* End File */
