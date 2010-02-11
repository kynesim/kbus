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
#include <stdbool.h>
#include <ctype.h>
#include <poll.h>
#include "kbus.h"

#define DEBUG 0

// ===========================================================================
// Ksock specific functions

/*
 * Open a Ksock.
 *
 * `device_number` indicates which KSock device to open, as
 * "/dev/kbus<device_number>".
 *
 * Which device numbers are available depends upon how many KBUS devices have
 * been initialised, either when the KBUS kernel module was installed, or by
 * use of `kbus_new_device()`.
 *
 * `flags` may be one of ``O_RDONLY``, ``O_WRONLY`` or ``O_RDWR``.
 *
 * Returns the file descriptor for the new Ksock, or a negative value on error.
 * The negative value will be ``-errno``.
 */
extern kbus_ksock_t kbus_ksock_open(uint32_t device_number,
                                    int      flags) 
{
  int   rv;
  int   mask  = O_RDONLY | O_WRONLY | O_RDWR;
  char  format[] = "/dev/kbus%u";
  char  filename[strlen(format) + 11];

  sprintf(filename, format, device_number);

  rv = open(filename, flags & mask);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Open a Ksock by device name. Since KBUS currrently only supports devices
 * of the for ``/dev/kbus<device_number>``, this function has no advantage
 * over `kbus_ksock_open``.
 *
 * `device_name` indicates which KSock device to open, as "/dev/kbus<device_number>",
 * where ``<device_number>`` is zero or more, depending on how many KBUS
 * devices are initialised.
 *
 * `flags` may be one of ``O_RDONLY``, ``O_WRONLY`` or ``O_RDWR``.
 *
 * Returns the file descriptor for the new Ksock, or a negative value on error.
 * The negative value will be ``-errno``.
 */
extern kbus_ksock_t kbus_ksock_open_by_name(const char *device_name,
                                            int         flags) 
{
  int mask  = O_RDONLY | O_WRONLY | O_RDWR;
  int rv = open(device_name, flags & mask);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Close a Ksock.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_close(kbus_ksock_t ksock) 
{
  int rv = close(ksock);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

// ===========================================================================
// Ksock IOCTL specific functions

/*
 * Bind the given message name to the specified Ksock.
 *
 * If `is_replier`, then bind as a Replier, otherwise as a Listener.
 *
 * Only one KSock at a time may be bound to a particular message as a Replier.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_bind(kbus_ksock_t         ksock,
                           const char          *name,
                           uint32_t             is_replier)
{
  int   rv;
  kbus_bind_request_t   bind_request;

  bind_request.name = (char *) name;
  bind_request.name_len = strlen(name);
  bind_request.is_replier = is_replier;

  rv = ioctl(ksock, KBUS_IOC_BIND, &bind_request);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Unbind the given message name to the specified Ksock.
 *
 * If `is_replier`, then unbind as a Replier, otherwise as a Listener.
 *
 * The unbinding must exactly match a previous binding (i.e., both message name
 * and `is_replier` must match).
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_unbind(kbus_ksock_t         ksock,
                             const char          *name,
                             uint32_t             is_replier)
{
  int   rv;
  kbus_bind_request_t   bind_request;

  bind_request.name = (char *) name;
  bind_request.name_len = strlen(name);
  bind_request.is_replier = is_replier;

  rv = ioctl(ksock, KBUS_IOC_UNBIND, &bind_request);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Return the internal (to KBUS) Ksock id for this Ksock.
 *
 * The Ksock id is a positive, non-zero number. It is used in message ``to``
 * and ``from`` fields.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_id(kbus_ksock_t   ksock,
                         uint32_t      *ksock_id) 
{
  int rv = ioctl(ksock, KBUS_IOC_KSOCKID, ksock_id);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Indicate that we wish to start reading the next message.
 *
 * Each Ksock has an (internal to KBUS) "next message" list. This function
 * pops the next message from that list, and makes it the "being read" message.
 * If there was still data for an earlier "being read" message, this will be
 * thrown away.
 *
 * `message_length` is set to the length of the message - that is, the value
 * to be passed to a subsequent call of ``kbus_ksock_next_msg()`` - or 0 if
 * there is no next message.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_next_msg(kbus_ksock_t     ksock,
                               uint32_t        *message_length) 
{
  int rv = ioctl(ksock, KBUS_IOC_NEXTMSG, message_length);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Find out how many bytes of the "being read" message are still to be read.
 *
 * `len_left` is set to the remaining number of bytes, or 0 if there are no
 * more bytes in the "being read" message, or if there is no "being read"
 * message (i.e., ``kbus_ksock_next_msg()`` has not been called since the
 * last message was finished or discarded).
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_len_left(kbus_ksock_t   ksock,
                               uint32_t      *len_left) 
{
  int rv = ioctl(ksock, KBUS_IOC_LENLEFT, len_left);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Determine the message id of the last message written on this Ksock.
 *
 * This will be {0,0} if there was no previous message.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_last_msg_id(kbus_ksock_t          ksock,
                                  kbus_msg_id_t        *msg_id)
{
  int rv = ioctl(ksock, KBUS_IOC_LASTSENT, msg_id);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Find the Ksock id of the Replier for the given message name.
 *
 * Returns 0 if there is no Replier bound to this message, 1 if the Replier's
 * Ksock id has been returned in `replier_ksock_id`, or a negative number
 * (``-errno``) for failure.
 */
extern int kbus_ksock_find_replier(kbus_ksock_t   ksock,
                                   const char    *name,
                                   uint32_t      *replier_ksock_id) 
{
  int                 rv;
  kbus_bind_query_t   bind_query;

  bind_query.name = (char *) name;
  bind_query.name_len = strlen(name);

  rv = ioctl(ksock, KBUS_IOC_REPLIER, &bind_query);
  if (rv < 0)
    return -errno;
  else {
    if (rv == 0)
      replier_ksock_id = 0;
    return rv;
  }
}

/*
 * Determine the number of (unread) messages that can be queued for this Ksock.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_max_messages(kbus_ksock_t   ksock,
                                   uint32_t      *max_messages) 
{
  int rv = ioctl(ksock, KBUS_IOC_MAXMSGS, max_messages);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Set the maximum number of (unread) messages that can be queued for this Ksock.
 *
 * If `num_messages` is greater than 0, then the maximum number of (unread)
 * messages that can be queued for this Ksock will be set.
 *
 * If 'num_messages' is 0, then the maximum is not changed - this may thus be
 * used to query the current maximum number of messages.
 *
 * Returns a positive number indicating the current (possibly just changed)
 * maximum number of messages, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_num_messages(kbus_ksock_t   ksock,
                                   uint32_t      *num_messages) 
{
  int rv = ioctl(ksock, KBUS_IOC_NUMMSGS, num_messages);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Determine the number of (unread) messages queued for this Ksock.
 *
 * Returns the current (unread) message count for this Ksock, or a negative
 * number (``-errno``) for failure.
 */
extern int kbus_ksock_num_unreplied_to(kbus_ksock_t   ksock,
                                       uint32_t      *num_messages) 
{
  int rv = ioctl(ksock, KBUS_IOC_UNREPLIEDTO, num_messages);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Send the last written message.
 *
 * Used to send a message when all of it has been written.
 *
 * Once the messge has been sent, the message and any name/data pointed to may
 * be freed.
 *
 * `msg_id` returns the message id assigned to the message by KBUS.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_send(kbus_ksock_t         ksock,
                           kbus_msg_id_t       *msg_id)
{
  int rv = ioctl(ksock, KBUS_IOC_SEND, msg_id);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Discard the message being written.
 *
 * Indicates that KBUS should throw away the (partial) message that has been
 * written. If there is no current message being written (for instance, because
 * ``kbus_ksock_send()`` has just been called), then this function has no
 * effect.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_discard(kbus_ksock_t         ksock)
{
  int rv = ioctl(ksock, KBUS_IOC_DISCARD);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

/*
 * Determine whether multiply-bound messages are only received once.
 *
 * Determine whether this Ksock should receive a particular message once, even
 * if it is both a Replier and Listener for the message, or if it is registered
 * more than once as a Listener for the message name.  
 *
 * Note that in the case of a Request that the Ksock should reply to, it will
 * always get the Request, and it will be the Listener's version of the message
 * that will be "dropped".
 *
 * If `request` is 1, then only one copy of the message is wanted.
 *
 * If `request` is 0, then as many copies as implied by the bindings are wanted.
 *
 * If `request` is 0xFFFFFFFF, then the number of copies is not to be changed.
 * This may be used to query the current state of the "only once" flag for this
 * Ksock.
 *
 * Beware that setting this flag affects how messages are added to the Ksock's
 * message queue *as soon as it is set* - so changing it and then changing it
 * back "at once" is not (necessarily) a null operation.
 *
 * Returns 0 or 1, according to the state of the "only once" flag *before* this
 * function was called, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_only_once(kbus_ksock_t   ksock,
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
  if (rv == 0)
    return rv;
  else if (rv < 0)
    return -errno;
  else
    return array[0];
}

/*
 * Determine whether Replier bind/unbind events should be reported.
 *
 * If `request` is 1, then each time a Ksock binds or unbinds as a Replier,
 * a Replier bind/unbind event should be sent (a "$.KBUS.ReplierBindEvent"
 * message).
 *
 * If `request` is 0, then Replier bind/unbind events should not be sent.
 *
 * If `request` is 0xFFFFFFFF, then the current state should not be changed.
 * This may be used to query the current state of the "send Replier bind event"
 * flag.
 *
 * Note that although this call is made via an individual Ksock, it affects the
 * behaviour of the entire KBUS device to which this Ksock is attached.
 *
 * Returns 0 or 1, according to the state of the "send Replier bind event" flag
 * *before* this function was called, or a negative number (``-errno``) for
 * failure.
 */
extern int kbus_ksock_report_replier_binds(kbus_ksock_t       ksock,
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
  rv = ioctl(ksock, KBUS_IOC_REPORTREPLIERBINDS, array);
  if (rv == 0)
    return rv;
  else if (rv < 0)
    return -errno;
  else
    return array[0];
}

/*
 * Request verbose kernel module messages.
 *
 * KBUS writes message via the normal kernel module mechanisms (which may be
 * inspected, for instance, via the ``dmesg`` command). Normal output is meant
 * to be reasonably minimal. Verbose messages can be useful for debugging the
 * kernel module.
 *
 * If `request` is 1, then verbose kernel messages are wanted.
 *
 * If `request` is 0, then verbose kernel messages are not wanted.
 *
 * If `request` is 0xFFFFFFFF, then the current state should not be changed.
 * This may be used to query the current state of the "verbose" flag.
 *
 * Note that although this call is made via an individual Ksock, it affects the
 * behaviour of the entire KBUS kernel module.
 *
 * Returns 0 or 1, according to the state of the "verbose" flag *before* this
 * function was called, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_kernel_module_verbose(kbus_ksock_t       ksock,
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
  if (rv == 0)
    return rv;
  else if (rv < 0)
    return -errno;
  else
    return array[0];
}

/*
 * Request the KBUS kernel module to create a new device (``/dev/kbus<n>``).
 *
 * `device_number` is the ``<n>`` for the new device.
 *
 * Note that it takes the kernel's hotplugging mechanisms a little while to
 * notice/activate the device, so do not expect it to be available immediately
 * on return.
 *
 * Note that although this call is made via an individual Ksock, it affects the
 * behaviour of the entire KBUS kernel module.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_new_device(kbus_ksock_t  ksock,
                                 uint32_t     *device_number) 
{
  int rv = ioctl(ksock, KBUS_IOC_NEWDEVICE, device_number);
  if (rv < 0)
    return -errno;
  else
    return rv;
}

// ===========================================================================
// Ksock read/write/etc.

/*
 * Wait until either the Ksock may be read from or written to.
 *
 * Returns when there is data to be read from the Ksock, or the Ksock
 * may be written to.
 *
 * `wait_for` indicates what to wait for. It should be set to
 * ``KBUS_SOCK_READABLE``, ``KBUS_SOCK_WRITABLE``, or the two "or"ed together,
 * as appropriate.
 *
 * This is a convenience routine for when polling indefinitely on a Ksock is
 * appropriate. It is not intended as a generic routine for any more
 * complicated situation, when specific "poll" (or "select") code should be
 * written.
 *
 * Returns ``KBUS_SOCK_READABLE``, ``KBUS_SOCK_WRITABLE``, or the two "or"ed
 * together to indicate which operation is ready, or a negative number
 * (``-errno``) for failure.
 */
extern int kbus_wait_for_message(kbus_ksock_t  ksock,
                                 int           wait_for)
{
  struct pollfd fds[1];
  int rv;

  fds[0].fd = (int)ksock;
  fds[0].events = ((wait_for & KBUS_KSOCK_READABLE) ? POLLIN  : 0) | 
                  ((wait_for & KBUS_KSOCK_WRITABLE) ? POLLOUT : 0);
  fds[0].revents =0;
  rv = poll(fds, 1, -1);
  if (rv < 0)
    return -errno;
  else 
    return ((fds[0].revents & POLLIN)  ? KBUS_KSOCK_READABLE : 0) |
           ((fds[0].revents & POLLOUT) ? KBUS_KSOCK_WRITABLE : 0);
}

/*
 * Read a message of length `msg_len` bytes from this Ksock.
 *
 * It is assumed that `msg_len` was returned by a previous call of
 * ``kbus_ksock_next_msg()``. It must be large enough to cause the entire
 * message to be read.
 *
 * `msg` is the message read. This will be an "entire" message, and should be
 * freed by the caller when no longer needed.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 * Specifically, -EBADMSG will be returned if the underlying ``read``
 * returned 0.
 */
extern int kbus_ksock_read_msg(kbus_ksock_t      ksock,
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

/*
 * Read the next message from this Ksock.
 *
 * This is equivalent to a call of ``kbus_ksock_next_msg()`` followed by a call
 * of ``kbus_ksock_read_msg()``.
 *
 * If there is no next message, ``msg`` will be NULL.
 *
 * If there is a next message, then ``msg`` will be the message read. This will
 * be an "entire" message, and should be freed by the caller when no longer
 * needed.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
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

/*
 * Write the given message to this Ksock. Does not send it.
 *
 * The `msg` may be an "entire" or "pointy" message.
 *
 * If the `msg` is a "pointy" message, then the name and any data must not be
 * freed until the message has been sent (as the pointers are only "followed"
 * when the message is sent).
 *
 * It is normally easier to use ``kbus_ksock_send_msg()``.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
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

/*
 * Write data to the Ksock. Does not send.
 *
 * This may be used to write message data in parts. It is normally better to use
 * the "whole message" routines.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_write_data(kbus_ksock_t    ksock,
                                 uint8_t        *data,
                                 size_t          data_len)
{
  size_t         written = 0;
  ssize_t        rv;

  while (written < data_len) {
    rv = write(ksock, data + written, data_len - written);
    if (rv > 0)
      written += rv;
    else if (rv < 0)
      return -errno;
  }
  return 0;
}

/*
 * Write and send a message on the given Ksock.
 *
 * This combines the "write" and "send" functions into one call, and is the
 * normal way to send a message.
 *
 * The `msg` may be an "entire" or "pointy" message.
 *
 * Once the messge has been sent, the message and any name/data pointed to may
 * be freed.
 *
 * `msg_id` returns the message id assigned to the message by KBUS.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_ksock_send_msg(kbus_ksock_t             ksock,
                               const kbus_message_t    *msg,
                               kbus_msg_id_t           *msg_id)
{
  int rv = kbus_ksock_write_msg(ksock, msg);
  if (rv) return rv;

  return kbus_ksock_send(ksock, msg_id);
}

// ===========================================================================
// Message specific functions

#define KBUS_BYTE_TO_WORD_LENGTH(x) ((x + 3) / 4)

/*
 * Create a message (specifically, a "pointy" message).
 *
 * Note that the message name and data are not copied, and thus should not be
 * freed until the message has been sent (with ``kbus_ksock_send_msg()``).
 *
 * `msg` is the new message, as created by this function.
 *
 * `name` is the name for the message, and `name_len` the length of the name
 * (the number of characters in the name). A message name is required.
 *
 * 'data' is the data for this message, or NULL if there is no data. `data_len`
 * is then the length of the data, in bytes.
 *
 * `flags` may be any KBUS message flags required. Most messages with flags set
 * can more easily be created by one of the other message creation routines.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_msg_create(kbus_message_t **msg, 
                           const char *name,
                           uint32_t name_len, /* bytes  */
                           const void *data,
                           uint32_t data_len, /* bytes */
                           uint32_t flags) 
{
  kbus_message_t *buf;
  size_t          length = sizeof(*buf);
  *msg = NULL;

  buf = malloc(length);
  if (!buf) return -ENOMEM;

  memset(buf, 0, length);

  buf->start_guard = KBUS_MSG_START_GUARD;
  buf->flags    = flags;
  buf->name_len = name_len;
  buf->data_len = data_len;
  buf->name = (char *) name;
  buf->data = (void *) data;
  buf->end_guard = KBUS_MSG_END_GUARD;
  
  *msg = buf;

  return 0;
}

/*
 * Create a short ("entire") message.
 *
 * Copies are taken of both `name` and `data`.
 *
 * "Entire" messages are limited in size (currently to 2048 bytes). That size
 * includes both the message header and the message data. Thus they are only
 * suitable for "short" messages.
 *
 * Unless you really, really need the "copying the name/data" functionality,
 * and are guaranteed to be sending short enough messages, please do not use
 * this function, use ``kbus_msg_create()`` instead.
 *
 * `msg` is the new message, as created by this function.
 *
 * `name` is the name for the message, and `name_len` the length of the name
 * (the number of characters in the name). A message name is required. The
 * name will be copied when the message is created.
 *
 * 'data' is the data for this message, or NULL if there is no data. `data_len`
 * is then the length of the data, in bytes. The data will be copied when the
 * message is created.
 *
 * `flags` may be any KBUS message flags required. Most messages with flags set
 * can more easily be created by one of the other message creation routines.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_msg_create_short(kbus_message_t        **msg, 
                                 const char             *name,
                                 uint32_t                name_len, /* bytes  */
                                 const void             *data,
                                 uint32_t                data_len, /* bytes */
                                 uint32_t                flags)
{
  int data_index = KBUS_ENTIRE_MSG_DATA_INDEX(name_len);
  int end_guard_index = KBUS_ENTIRE_MSG_END_GUARD_INDEX(name_len,data_len);
  kbus_entire_message_t *buf;
  size_t length = KBUS_ENTIRE_MSG_LEN(name_len, data_len);

  *msg = NULL;

  if (length > KBUS_MAX_ENTIRE_LEN)
    return -EMSGSIZE;
 
  buf = malloc(length);
  if (!buf) return -ENOMEM;

  memset(buf, 0, length);

  buf->header.start_guard = KBUS_MSG_START_GUARD;
  buf->header.flags       = flags;
  buf->header.name_len    = name_len;
  buf->header.data_len    = data_len;
  buf->header.end_guard   = KBUS_MSG_END_GUARD;

  memcpy(&buf->rest[0],  name, name_len);
  memcpy(&buf->rest[data_index], data, data_len);

  buf->rest[end_guard_index] = KBUS_MSG_END_GUARD;

  *msg = (kbus_message_t *) buf;

  return 0;
}

/*
 * Create a Request (specifically, a "pointy" Request message).
 *
 * Note that the message name and data are not copied, and thus should not be
 * freed until the message has been sent (with ``kbus_ksock_send_msg()``).
 *
 * `msg` is the new message, as created by this function.
 *
 * `name` is the name for the message, and `name_len` the length of the name
 * (the number of characters in the name). A message name is required.
 *
 * 'data' is the data for this message, or NULL if there is no data. `data_len`
 * is then the length of the data, in bytes.
 *
 * `flags` may be any KBUS message flags required. These will be set on the
 * message, and then (after that) the KBUS_BIT_WANT_A_REPLY flag will be set
 * to make the new message a Request.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_msg_create_request(kbus_message_t **msg, 
                                   const char *name,
                                   uint32_t name_len, /* bytes  */
                                   const void *data,
                                   uint32_t data_len, /* bytes */
                                   uint32_t flags) 
{
  int rv = kbus_msg_create(msg, name, name_len, data, data_len, flags);
  if (rv) return rv;

  (*msg)->flags |= KBUS_BIT_WANT_A_REPLY;

  return 0;
}

/*
 * Create a short ("entire") Request message.
 *
 * "Entire" messages are limited in size (currently to 2048 bytes). That size
 * includes both the message header and the message data. Thus they are only
 * suitable for "short" messages.
 *
 * Unless you really, really need the "copying the name/data" functionality,
 * and are guaranteed to be sending short enough messages, please do not use
 * this function, use ``kbus_msg_create()`` instead.
 *
 * This is identical in behaviour to ``kbus_msg_create_request()``, except
 * that an "entire" message is created, and thus both the message name and data
 * are copied. This means that the original `name` and `data` may be freed as
 * soon as the `msg` has been created.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_msg_create_short_request(kbus_message_t        **msg, 
                                         const char             *name,
                                         uint32_t                name_len, /* bytes  */
                                         const void             *data,
                                         uint32_t                data_len, /* bytes */
                                         uint32_t                flags)
{
  int rv = kbus_msg_create_short(msg, name, name_len, data, data_len, flags);
  if (rv) return rv;

  (*msg)->flags |= KBUS_BIT_WANT_A_REPLY;

  return 0;
}

/*
 * Create a Reply message, based on a previous Request.
 *
 * This is a convenience mechanism for creating the Reply to a previous
 * Request.
 *
 * The Request must have been marked as wanting this particular recipient to
 * reply to it (i.e., ``kbus_msg_wants_us_to_reply()`` returns true). If this
 * is not so, -EBADMSG will be returned.
 *
 * `msg` is the new Reply message. `in_reply_to` is the Request message for
 * which a Reply is wanted.
 *
 * The message name for the new message will be taken from the old message.
 *
 * The 'to' field for the new message will be set to the 'from' field in the old.
 *
 * The 'in_reply_to' field for the new message will be set to the message id of the old.
 *
 * 'data' is the data for the new message, or NULL if there is none. 'data_len'
 * is the length of the data, in bytes.
 *
 * As normal, the message name and data should not be freed until `msg` has
 * been sent. In the normal case, where `in_reply_to` is an "entire" message
 * read from KBUS, this means that `in_reply_to` and `data` should not be
 * freed, since the message name "inside" `in_reply_to` is being referenced.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_msg_create_reply(kbus_message_t **msg, 
                                 const kbus_message_t *in_reply_to,
                                 const void *data,
                                 uint32_t data_len, /* bytes */
                                 uint32_t flags)
{
  char *name;
  int   rv;

  if (!kbus_msg_wants_us_to_reply(in_reply_to))
    return -EBADMSG;

  name = kbus_msg_name_ptr(in_reply_to);
  rv = kbus_msg_create(msg, name, in_reply_to->name_len, data, data_len, flags);
  if (rv) return rv;

  (*msg)->to          = in_reply_to->from;
  (*msg)->in_reply_to = in_reply_to->id;
  return 0;
}

/*
 * Create a short ("entire") Reply message, based on a previous Request.
 *
 * "Entire" messages are limited in size (currently to 2048 bytes). That size
 * includes both the message header and the message data. Thus they are only
 * suitable for "short" messages.
 *
 * Unless you really, really need the "copying the name/data" functionality,
 * and are guaranteed to be sending short enough messages, please do not use
 * this function, use ``kbus_msg_create_reply()`` instead.
 *
 * This is identical in behaviour to ``kbus_msg_create_reply()``, except
 * that an "entire" message is created, and thus both the message name and data
 * are copied. This means that the original (`in_reply_to`) message and the
 * `data` may be freed as soon as the `msg` has been created.
 * 
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_msg_create_short_reply(kbus_message_t          **msg, 
                                       const kbus_message_t     *in_reply_to,
                                       const void               *data, 
                                       uint32_t                  data_len, /* bytes */
                                       uint32_t                  flags)
{
  char *name;
  int   rv;

  if (!kbus_msg_wants_us_to_reply(in_reply_to))
    return -EBADMSG;

  name = kbus_msg_name_ptr(in_reply_to);
  rv = kbus_msg_create_short(msg, name, in_reply_to->name_len, 
                             data, data_len, flags);
  if (rv) return rv;

  (*msg)->to          = in_reply_to->from;
  (*msg)->in_reply_to = in_reply_to->id;
  return 0;
}

/*
 * Create a Stateful Request message, based on a previous Reply or Request.
 *
 * This is a convenience mechanism for creating a Stateful Request message
 * (a Request which must be delivered to a particular Ksock).
 *
 * `msg` is the new Stateful Request message.
 *
 * `earlier_msg` is either a Reply message from the desired Ksock, or a
 * previous Stateful Request to the same Ksock.
 *
 * If the earlier message is a Reply, then the 'to' and 'final_to' fields for
 * the new message will be set to the 'from' and 'orig_from' fields in the old.
 *
 * If the earlier message is a Stateful Request, then the 'to' and 'final_to'
 * fields for the new message will be copied from the old.
 *
 * If the earlier message is neither a Reply nor a Stateful Request, then
 * -EBADMSG will be returned.
 *
 * 'name' is the name for the new message, and 'name_len' is the length of that
 * name.
 *
 * 'data' is the data for the new message, or NULL if there is none. 'data_len'
 * is the length of the data, in bytes.
 *
 * 'flags' is any KBUS flags to set on the message (flags will not be copied
 * from the earlier message).
 *
 * As normal, the message name and data should not be freed until `msg` has
 * been sent. `earlier_msg` may be freed after this call has completed, as
 * any necessary data will have been copied from it.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_msg_create_stateful_request(kbus_message_t         **msg, 
                                            const kbus_message_t    *earlier_msg,
                                            const char          *name,
                                            uint32_t             name_len,
                                            const void          *data, 
                                            uint32_t             data_len, /* bytes */
                                            uint32_t             flags)
{
  int                   rv;
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
    return -EBADMSG;
  }

  rv = kbus_msg_create(msg, name, name_len, data, data_len, flags);
  if (rv) return rv;

  (*msg)->final_to = final_to;
  (*msg)->to       = to;
  return 0;
}

/*
 * Create a Stateful Request message, based on a previous Reply or Request.
 *
 * "Entire" messages are limited in size (currently to 2048 bytes). That size
 * includes both the message header and the message data. Thus they are only
 * suitable for "short" messages.
 *
 * Unless you really, really need the "copying the name/data" functionality,
 * and are guaranteed to be sending short enough messages, please do not use
 * this function, use ``kbus_msg_create_stateful_request()`` instead.
 *
 * This is identical in behaviour to ``kbus_msg_create_stateful_request()``,
 * except that an "entire" message is created, and thus both the message name
 * and data are copied. This means that both the `name` and the `data` may be
 * freed as soon as the `msg` has been created.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_msg_create_short_stateful_request(kbus_message_t       **msg, 
                                                  const kbus_message_t  *earlier_msg,
                                                  const char            *name,
                                                  uint32_t               name_len,
                                                  const void            *data, 
                                                  uint32_t               data_len, /* bytes */
                                                  uint32_t               flags)
{
  int                   rv;
  uint32_t              to;
  struct kbus_orig_from final_to;

  if (kbus_msg_is_reply(earlier_msg)) {
    final_to = earlier_msg->orig_from;
    to = earlier_msg->from;
  } else if (kbus_msg_is_stateful_request(earlier_msg)) {
    final_to = earlier_msg->final_to;
    to = earlier_msg->to;
  } else {
    return -EBADMSG;
  }

  rv = kbus_msg_create_short(msg, name, name_len, data, data_len, flags);
  if (rv) return rv;

  (*msg)->final_to = final_to;
  (*msg)->to       = to;
  return 0;
}

/*
 * Delete a message datastructure.
 *
 * Does nothing if `msg_p` is NULL, or `*msg_p` is NULL.
 *
 * Frees the message datastructure, but does not free any name or data that is
 * pointed to.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern void kbus_msg_delete(kbus_message_t **msg_p)
{
  if (msg_p && *msg_p) {
    kbus_message_t *msg = (kbus_message_t *)(*msg_p);
    free(msg);
    (*msg_p) = NULL;
  } 
  return;
}

/*
 * Determine the size of a KBUS message.
 *
 * For a "pointy" message, returns the size of the message header.
 *
 * For an "entire" message, returns the size of the entire message.
 *
 * In either case, this is the length of data that would (for instance)
 * be written to a Ksock to actually write the message. In other words::
 *
 *   int len, rv;
 *   len = kbus_msg_sizeof(&msg);
 *   rv = kbus_ksock_write_data(ksock, &msg, len);
 *   if (rv < 0) return rv;
 *
 * is the "low level" equivalent of::
 *
 *   int rv = kbus_ksock_write_msg(ksock, &msg);
 *   if (rv < 0) return rv;
 *
 * Returns the length of 'msg', as described above.
 */
extern int kbus_msg_sizeof(const kbus_message_t *msg)
{
  if (kbus_msg_is_entire(msg)) {
    return KBUS_ENTIRE_MSG_LEN(msg->data_len, msg->name_len);
  } else {
    return sizeof(*msg);
  }
}

/*
 * A convenience routine to split the data of a Replier bind event.
 *
 * Replier bind events contain the following information:
 *
 * * `is_replier` is true if the event was a "bind", false it if was an
 *   "unbind".
 * * `binder` is the Ksock id of the binder.
 * * `name` is the name of the message that was being (un)bound.
 *
 * Note that `name` is a copy of the name (from the original `msg`), so that
 * the user may free the original message immediately. Clearly this copy will
 * also need freeing when finished with.
 *
 * Returns 0 for success, or a negative number (``-errno``) for failure.
 */
extern int kbus_msg_split_bind_event(const kbus_message_t  *msg,
                                     uint32_t              *is_bind,
                                     uint32_t              *binder,
                                     char                 **name)
{
  struct kbus_replier_bind_event_data  *event_data;
  char                                 *orig_name;
  char                                 *this_name;
  void *data = kbus_msg_data_ptr(msg);

  // This is the barest of plausibility checks
  if (!data || msg->data_len==0) return -EBADMSG;

  event_data = (struct kbus_replier_bind_event_data *)data;
  orig_name = (char *)event_data->rest;

  this_name = malloc(event_data->name_len + 1);
  if (!this_name) return -ENOMEM;

  strncpy(this_name, orig_name, event_data->name_len);
  this_name[event_data->name_len+1] = 0;

  *is_bind = event_data->is_bind;
  *binder = event_data->binder;
  *name = this_name;

  return 0;
}

/*
 * Print our a representation of a message.
 *
 * `stream` is the output stream to print to -- typically stdout.
 *
 * Does not print a newline.
 */
extern void kbus_msg_print(FILE                 *stream,
                           const kbus_message_t *msg)
{
  int is_bind_event = false;
  char *name = kbus_msg_name_ptr(msg);
  char *data = kbus_msg_data_ptr(msg);

  fprintf(stream,"<");

  if (kbus_msg_is_reply(msg)) {
    if (msg->name_len > 7 && !strncmp("$.KBUS.", name, 7))
      fprintf(stream,"Status");
    else
      fprintf(stream,"Reply");
  } else if (kbus_msg_is_request(msg)) {
    fprintf(stream,"Request");
  } else {
    if (msg->name_len == strlen(KBUS_MSG_NAME_REPLIER_BIND_EVENT) &&
        !strncmp(KBUS_MSG_NAME_REPLIER_BIND_EVENT, name,
                 strlen(KBUS_MSG_NAME_REPLIER_BIND_EVENT))) {
      fprintf(stream,"ReplierBindEvent");
      is_bind_event = true;
    } else {
      fprintf(stream,"Announcement");
    }
  }

  if (!is_bind_event)
    fprintf(stream," '%.*s'",msg->name_len,name);

  if (msg->id.network_id != 0 || msg->id.serial_num != 0)
    fprintf(stream," id=[%u:%u]", msg->id.network_id, msg->id.serial_num);

  if (msg->to != 0)
    fprintf(stream," to=%u", msg->to);

  if (msg->from != 0)
    fprintf(stream," from=%u", msg->from);

  if (msg->orig_from.network_id != 0 || msg->orig_from.local_id != 0)
    fprintf(stream," orig_from=(%u,%u)", msg->orig_from.network_id, msg->orig_from.local_id);

  if (msg->final_to.network_id != 0 || msg->final_to.local_id != 0)
    fprintf(stream," final_to=(%u,%u)", msg->final_to.network_id, msg->final_to.local_id);

  if (msg->in_reply_to.network_id != 0 || msg->in_reply_to.serial_num != 0)
    fprintf(stream," in_reply_to=[%u:%u]", msg->in_reply_to.network_id, msg->in_reply_to.serial_num);

  if (msg->flags) {
    fprintf(stream," flags=%08x", msg->flags);
    if (msg->flags & KBUS_BIT_WANT_A_REPLY) fprintf(stream," REQ");
    if (msg->flags & KBUS_BIT_WANT_YOU_TO_REPLY) fprintf(stream," YOU");
    if (msg->flags & KBUS_BIT_SYNTHETIC) fprintf(stream," SYN");
    if (msg->flags & KBUS_BIT_URGENT) fprintf(stream," URG");
    if (msg->flags & KBUS_BIT_ALL_OR_FAIL) fprintf(stream," aFL");
    if (msg->flags & KBUS_BIT_ALL_OR_WAIT) fprintf(stream," aWT");
  }

  if (msg->data_len > 0) {
    fprintf(stream," data=");
    if (is_bind_event) {
      uint32_t  is_bind, binder;
      char     *bind_name = NULL;
      (void) kbus_msg_split_bind_event(msg, &is_bind, &binder, &bind_name);

      fprintf(stream," [%s '%s' for %u]",(is_bind?"Bind":"Unbind"), bind_name, binder);

      if (bind_name) free(bind_name);
    } else {
      int ii, minlen = msg->data_len<20?msg->data_len:20;
      for (ii=0; ii<minlen; ii++) {
        char  ch = data[ii];
        if (isprint(ch))
          fprintf(stream,"%c",ch);
        else
          fprintf(stream,"\\x%02x",ch);
      }
      if (msg->data_len > 20)
        fprintf(stream,"...");
    }
  }
  fprintf(stream,">");
}

/*
 * Print out (on stdout) information about a message.
 *
 * If `dump_data` is true, also print out the message data (in several forms).
 */
extern void kbus_msg_dump(const kbus_message_t *msg,
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

// Local Variables:
// tab-width: 8
// indent-tabs-mode: nil
// c-basic-offset: 2
// End:
// vim: set tabstop=8 shiftwidth=2 softtabstop=2 expandtab:
