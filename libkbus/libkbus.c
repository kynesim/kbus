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
 *   Tony Ibbs <tibs@tibsnjoan.co.uk>
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
#include <ctype.h>
#include <sys/poll.h>
#include "kbus.h"

#define DEBUG 0

// ===========================================================================
// Ksock specific functions

kbus_ksock_t kbus_ksock_open(const char *fname,
                             int         flags) 
{
  int mask  = O_RDONLY | O_WRONLY | O_RDWR;
  return open(fname, flags & mask);
}

int kbus_ksock_close(kbus_ksock_t ksock) 
{
  return close(ksock);
}

// ===========================================================================
// Ksock IOCTL specific functions

int kbus_ksock_bind(kbus_ksock_t         ksock,
                    const char          *name,
                    uint32_t             is_replier)
{
  kbus_bind_request_t   bind_request;

  bind_request.name = (char *) name;
  bind_request.name_len = strlen(name);
  bind_request.is_replier = is_replier;

  return ioctl(ksock, KBUS_IOC_BIND, &bind_request);
}

int kbus_ksock_only_once(kbus_ksock_t   ksock,
                         uint32_t       request)
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
  rv = ioctl(ksock, KBUS_IOC_MSGONLYONCE, array);
  if (rv)
    return rv;
  else
    return array[0];
}

int kbus_ksock_kernel_module_verbose(kbus_ksock_t       ksock,
                                     uint32_t           request)
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
  rv = ioctl(ksock, KBUS_IOC_VERBOSE, array);
  if (rv)
    return rv;
  else
    return array[0];
}

int kbus_ksock_id(kbus_ksock_t   ksock,
                  uint32_t      *ksock_id) 
{
  return ioctl(ksock, KBUS_IOC_KSOCKID, ksock_id);
}

int kbus_ksock_next_msg(kbus_ksock_t     ksock,
                        uint32_t        *len) 
{
  return ioctl(ksock, KBUS_IOC_NEXTMSG, len);
}

int kbus_ksock_send(kbus_ksock_t         ksock,
                    kbus_msg_id_t       *msg_id)
{
  return ioctl(ksock, KBUS_IOC_SEND, msg_id);
}

int kbus_ksock_new_device(kbus_ksock_t  ksock,
                          uint32_t     *device_number) 
{
  return ioctl(ksock, KBUS_IOC_NEWDEVICE, device_number);
}

// ===========================================================================
// Ksock read/write/etc.

int kbus_wait_for_message(kbus_ksock_t  ksock,
                          int           wait_for)
{
  struct pollfd fds[1];
  int rv;

  fds[0].fd = (int)ksock;
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

int kbus_ksock_read_msg(kbus_ksock_t      ksock,
                        kbus_message_t  **msg, 
                        size_t            msg_len) 
{
  ssize_t        so_far = 0;
  ssize_t        length = 0;
  char          *buf;
 
  buf = malloc(msg_len);
  if (!buf) return -ENOMEM;

  while (msg_len > 0) {
    length = read(ksock, buf+so_far, msg_len-so_far);
#if DEBUG
    printf("attemping to read %d bytes, read %d\n", msg_len-so_far, length);
#endif
    if (length > 0) {
      msg_len -= length;
      so_far += length; 
    } else if (length == 0) {
      free(buf);
      *msg = NULL;
      return -EBADMSG;
    } else {
      if (errno != EAGAIN && errno != EINTR) {
        free(buf);
        *msg = NULL;
        return errno;
      }
    }
  }

  *msg = (kbus_message_t *)buf;
  return 0;
}

extern int kbus_ksock_read_next_msg(kbus_ksock_t          ksock,
                                    kbus_message_t      **msg)
{
  int           rv;
  uint32_t      msg_len;

  rv = kbus_ksock_next_msg(ksock, &msg_len);
  if (rv < 0 || msg_len == 0) {
    *msg = NULL;
    return rv;
  }

  rv = kbus_ksock_read_msg(ksock, msg, msg_len);

#if DEBUG
  kbus_msg_dump(*msg, true);
#endif

  return rv;
}

extern int kbus_ksock_write_msg(kbus_ksock_t             ksock,
                                const kbus_message_t    *msg)
{
  size_t         length;
  size_t         written = 0;
  ssize_t        rv;
  uint8_t       *data = (uint8_t *)msg;

  if (kbus_msg_is_entire(msg))
    length = KBUS_ENTIRE_MSG_LEN(msg->name_len, msg->data_len);
  else
    length = sizeof(*msg);
  
  while (written < length) {
    rv = write(ksock, data + written, length - written);
    
    if (rv > 0)
      written += rv;
    else if (rv < 0)
      return -errno;
  }
  return 0;
}

int kbus_ksock_send_msg(kbus_ksock_t             ksock,
                        const kbus_message_t    *msg,
                        kbus_msg_id_t           *msg_id)
{
  int rv;
  rv = kbus_ksock_write_msg(ksock, msg);

  if (rv)
    return rv;

  rv = kbus_ksock_send(ksock, msg_id);

  return rv;
}

// ===========================================================================
// Message specific functions

#define KBUS_BYTE_TO_WORD_LENGTH(x) ((x + 3) / 4)

int kbus_msg_create_short(kbus_message_t        **msg, 
			  const char             *name,
                          uint32_t                name_len, /* bytes  */
			  const void             *data,
                          uint32_t                data_len, /* bytes */
			  uint32_t                flags)
{
  int di = KBUS_ENTIRE_MSG_DATA_INDEX(name_len);
  int eg = KBUS_ENTIRE_MSG_END_GUARD_INDEX(name_len,data_len);
  kbus_entire_message_t *buf;
  size_t length = KBUS_ENTIRE_MSG_LEN(name_len, data_len);

  *msg = NULL;

  if (length > KBUS_MAX_ENTIRE_LEN) {
    errno = EMSGSIZE;
    goto fail;
  }
 
  buf    = malloc(length);
 
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

  *msg = (kbus_message_t *) buf;

  return 0;

 fail:
  if (buf)
    free(buf);

  return -1;
}

int kbus_msg_create(kbus_message_t **msg, 
		    const char *name,
                    uint32_t name_len, /* bytes  */
		    const void *data,
                    uint32_t data_len, /* bytes */
		    uint32_t flags) 
{
  *msg = NULL;

  kbus_message_t *buf;
 
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
  
  *msg = buf;

  return 0;

 fail:
  if (buf)
    free(buf);

  return -1;
}

int kbus_msg_create_short_reply(kbus_message_t **msg, 
				const kbus_message_t *in_reply_to,
				const void *data, 
				uint32_t data_len, /* bytes */
				uint32_t flags)
{
  char *name;
  int rv;

  if (!kbus_msg_wants_us_to_reply(in_reply_to)) {
    errno = EBADMSG;
    return -1;
  }

  name = kbus_msg_name_ptr(in_reply_to);
  rv = kbus_msg_create_short(msg, name, in_reply_to->name_len, 
			     data, data_len, flags);

  if (rv) {
    return -1;
  }

  (*msg)->to          = in_reply_to->from;
  (*msg)->in_reply_to = in_reply_to->id;
  return 0;
}

int kbus_msg_create_reply(kbus_message_t **msg, 
			  const kbus_message_t *in_reply_to,
			  const void *data,
                          uint32_t data_len, /* bytes */
			  uint32_t flags)
{
  char *name;
  int rv;

  if (!kbus_msg_wants_us_to_reply(in_reply_to)) {
    errno = EBADMSG;
    return -1;
  }

  name = kbus_msg_name_ptr(in_reply_to);
  rv = kbus_msg_create(msg, name, in_reply_to->name_len, data, data_len, flags);

  if (rv){
    return -1;
  }

  (*msg)->to          = in_reply_to->from;
  (*msg)->in_reply_to = in_reply_to->id;
  return 0;
}

int kbus_msg_create_short_stateful_request(kbus_message_t         **msg, 
                                           const kbus_message_t    *earlier_msg,
                                           const char          *name,
                                           uint32_t             name_len,
                                           const void          *data, 
                                           uint32_t             data_len, /* bytes */
                                           uint32_t             flags)
{
  int rv;

  uint32_t              to;
  struct kbus_orig_from final_to;

  if (kbus_msg_is_reply(earlier_msg)) {
    final_to = earlier_msg->orig_from;
    to = earlier_msg->from;
  }
  else if (kbus_msg_is_stateful_request(earlier_msg)) {
    final_to = earlier_msg->final_to;
    to = earlier_msg->to;
  }
  else {
    errno = EBADMSG;
    return -1;
  }

  rv = kbus_msg_create_short(msg, name, name_len, data, data_len, flags);

  if (rv) {
    return -1;
  }

  (*msg)->final_to = final_to;
  (*msg)->to       = to;
  return 0;
}

int kbus_msg_create_stateful_request(kbus_message_t         **msg, 
                                     const kbus_message_t    *earlier_msg,
                                     const char          *name,
                                     uint32_t             name_len,
                                     const void          *data, 
                                     uint32_t             data_len, /* bytes */
                                     uint32_t             flags)
{
  int rv;

  uint32_t              to;
  struct kbus_orig_from final_to;

  if (kbus_msg_is_reply(earlier_msg)) {
    final_to = earlier_msg->orig_from;
    to = earlier_msg->from;
  }
  else if (kbus_msg_is_stateful_request(earlier_msg)) {
    final_to = earlier_msg->final_to;
    to = earlier_msg->to;
  }
  else {
    errno = EBADMSG;
    return -1;
  }

  rv = kbus_msg_create(msg, name, name_len, data, data_len, flags);

  if (rv) {
    return -1;
  }

  (*msg)->final_to = final_to;
  (*msg)->to       = to;
  return 0;
}

void kbus_msg_dispose(kbus_message_t **kms_p)
{
  if (!kms_p || !*kms_p) { return; }

  kbus_message_t *msg = (kbus_message_t *)(*kms_p);

  /* We allocated enough space all in one go so just free */
  free(msg);
  (*kms_p) = NULL;
  
  return;
}

int kbus_msg_sizeof(const kbus_message_t *msg)
{
  int len;

  if (kbus_msg_is_entire(msg)) {
    len = KBUS_ENTIRE_MSG_LEN(msg->data_len, msg->name_len);
  } else {
    len = sizeof(*msg);
  }

  return len;
}


void kbus_msg_dump(const kbus_message_t *msg,
                   int                   dump_data) 
{
  int i;

  printf("Message: %p\n", msg);

  printf("  start guard: %08x\n", msg->start_guard);

  printf("  id:          {%u,%u}\n", msg->id.network_id, msg->id.serial_num);
  printf("  in_reply_to: {%u,%u}\n", msg->in_reply_to.network_id, msg->in_reply_to.serial_num);
  printf("  to:          %u\n", msg->to);
  printf("  from:        %u\n", msg->from);

  printf("  orig_from:   {%u,%u}\n", msg->orig_from.network_id, msg->orig_from.local_id);
  printf("  final_to:    {%u,%u}\n", msg->final_to.network_id, msg->final_to.local_id);
  
  printf("  flags:       %08x\n", msg->flags);
  printf("  name_len:    %u\n", msg->name_len);
  printf("  data_len:    %u\n", msg->data_len);
  printf("  name (ptr):  %p\n", msg->name);
  printf("  data (ptr):  %p\n", msg->data);
  printf("  end guard:   %08x\n", msg->end_guard);
  printf("\n");
  printf("  Message name:   ");

  char *name_ptr = kbus_msg_name_ptr(msg);

  for (i = 0; i < msg->name_len; i ++) {
    if (isgraph(name_ptr[i]) || name_ptr[i] == ' ')
      printf("%c", name_ptr[i]);
    else 
      printf("?");
  }

  char *data_ptr = kbus_msg_data_ptr(msg);

  printf("\n  Data (text):    ");
  char *data_cptr = (char *)data_ptr;
  for (i = 0; i < msg->data_len; i ++) {
    if (isgraph(data_cptr[i]) || data_cptr[i] == ' ')
      printf("%c", data_cptr[i]);
    else 
      printf("?");
  }

  printf("\n  Data (bytes):   ");
  int data_limit = (msg->data_len);
  for (i = 0; i < data_limit; i ++) {
    if (i != 0 && !(i % 16)) {
      printf("\n  ");
    }
    printf("%02x ",data_ptr[i]);
  }

  printf("\n  Whole message (bytes):");
  uint8_t *dptr = (uint8_t *)msg;
  for (i = 0; i < kbus_msg_sizeof(msg); i ++) {
    if (!(i % 26)) {
      printf("\n  ");
    }
    printf("%02x ", dptr[i]);

  }
  printf("\n");
}

/* End File */
