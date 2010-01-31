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

#ifndef _LKBUS_H_INCLUDED_
#define _LKBUS_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif 

#include <kbus/kbus_defns.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef struct kbus_msg_str {
  struct kbus_message_header header;
} kbus_msg_t;

typedef struct kbus_entire_message kbus_msg_entire_t;

/*
 * TODO: I'd prefer to use this insted of the above. 
 */
typedef struct kbus_message_header kbus_message_t;


/** A Ksock is just a file descriptor, an integer, as returned by 'open'.
 */
typedef int kbus_ksock_t;
/* For compatibility with earlier versions of the library, we retain:
 *
 * TODO: make this deprecated...
 */
typedef int ksock;

/* Ksock Functions */

/** @file
 *
 * Note that all of the functions here are non-blocking: there is no such
 * thing as a synchronous kbus socket (though there are wait() functions here
 * to emulate one).
 */

/** Test if this is an entire message.
 *
 * @param msg A kbus message header
 * @return Non-zero for an entire message, zero for pointy
 */
#define KBUS_MSG_IS_ENTIRE(msg) (((msg)->header.name)? 0 : 1)
#define KBUS_MSG_TO_ENTIRE(msg) ((kbus_msg_entire_t *)(msg))
#define KBUS_MSG_TO_HEADER(msg) ((struct kbus_message_header *)(msg))
#define KBUS_ENTIRE_TO_MSG(msg) ((kbus_msg_t *)(msg))


/** Open a ksock
 *
 * @param fname IN  KBus filename - /dev/kbusNN and typically /dev/kbus0.
 * @param flags IN  One of O_RDONLY , O_WRONLY, O_RDWR .
 * @return A socket file descriptor, or < 0 on error, in which case
 *    errno contains a description of the error.
 */
kbus_ksock_t kbus_ksock_open           (const char *fname, int flags);

/** Close a ksock 
 *
 * @param ks  IN  ksock to close.
 */
int   kbus_ksock_close          (kbus_ksock_t ks);

/** Bind a ksock to the given KBus name.
 *
 * @param name IN  Name to bind to. This can go away once the call returns.
 * @return 0 on success,  < 0 setting errno on failure.
 */
int   kbus_ksock_bind           (kbus_ksock_t ks, const char *name, uint32_t replier);

/** Indicate/query if multiply bound messages should be received only once.
 *
 * By default, if a Ksock binds a message name more than once, it will receive
 * a copy of a message (with that name) for each binding.
 *
 * This function can be used to request that only once copy of the message is
 * received. Note that if one of the messages is a Request to which this Ksock
 * is bound as Replier, then it will always be the Request marked as "you must
 * reply" that will be received.
 *
 * @param request IN    This should be 0 to choose the default (as many
 * messages as bindings), 1 to choose "only once" mode, and 0xFFFFFFFF
 * to just return the current choice, without changing it.
 * @return 0 or 1 (the current state) on success, < 0 on failure.
 */
int   kbus_ksock_only_once(kbus_ksock_t ks, uint32_t request);

/** Indicate/query if a Ksock should output verbose kernel messages.
 *
 * By default, a Ksock does not output verbose messages to the kernel log.
 *
 * This function can be used to request that verbose messages be output (or not).
 * It may be used by any "open" Ksock interface to affect the whole device.
 * It will only have effect if the kernel module was built with the VERBOSE_DEBUG
 * flag set.
 *
 * @param request IN    This should be 0 to choose minimal messages (the
 * default), 1 to choose "verbose" mode, and 0xFFFFFFFF to just return the
 * current choice, without changing it.
 * @return 0 or 1 (the current state) on success, < 0 on failure.
 */
int   kbus_ksock_kernel_module_verbose(kbus_ksock_t ks, uint32_t request);

/** Return a number describing this ksock endpoint uniquely for this
 *  (local) kbus instance; can be used to decide if two distinct fds
 *  point to separate ksocks.
 *
 * @param[in]  ks        The ksock to query.
 * @param[out] ksock_id  On success, filled with a number describing this ksock
 * @return 0 on success , <0 setting errno on failure.
 */
int   kbus_ksock_id             (kbus_ksock_t ks, uint32_t *ksock_id);

#define KBUS_KSOCK_READABLE 1
#define KBUS_KSOCK_WRITABLE 2

/** Wait until there is something to read on this kbus socket and/or
 *  we can write.
 *
 * @param wait_for an OR of KBUS_KSOCK_READABLE, KBUS_KSOCK_WRITABLE
 * @return An OR of KBUS_KSOCK_READABLE and KBUS_KSOCK_WRITABLE , < 0 on
 *   failure and set errno
 */
int kbus_wait_for_message       (kbus_ksock_t ks, int wait_for);

/** Move on to the next message waiting on this ksock, returning its
 *  length.
 * 
 * @param[in] ks      The ksock to query.
 * @param[out] len    On success, the length of the next message
 * @return 0 if there isn't a next message, 1 if there is, < 0 and sets errno
 *         on error.
 */
int   kbus_ksock_next_msg       (kbus_ksock_t ks, uint32_t *len);

/** If there is a message waiting, read and return it. Otherwise just
 *  return. Note that we assume that the current message has been 
 *  processed (the first thing we do is to call next_msg()).
 *
 * @param[in] ks     The ksock to query.
 * @param[out] msg   On success, filled in with the next message on this ksock in
 *                   malloc()'d storage. Filled in with NULL otherwise.
 * @return 0 if there wasn't a message waiting, 1 if one has been delivered in 
 *    (*msg), < 0 and fills in errno on failure.
 */
int   kbus_ksock_read_next_msg  (kbus_ksock_t ks, kbus_msg_t **msg);

/** Read the current message on the ksock, given its length.
 *
 * @param[in] ks    The ksock to query.
 * @param[out] msg  On success, filled in with the next message on this ksock.
 *                   Otherwise filled in with NULL.
 * @return 1 if (*msg) now contains your message, 0 if it doesn't, < 0 and 
 *             set errno on failure.
 */
int   kbus_ksock_read_msg       (kbus_ksock_t ks, kbus_msg_t **msg, 
                                 size_t len);

/** Write a message (but do not send it yet). A copy of the message is
 *  taken so you can dispose of the message after this function returns
 *  successfully.
 *
 *  You probably wanted to use kbus_ksock_send_msg() instead of this
 *  function.
 *
 * @param[in]   ks   The ksock to send the message with.
 * @param[in]   msg  The message to send.
 * @return 0 on success, < 0 and sets errno on failure.
 */
int   kbus_ksock_write_msg      (kbus_ksock_t ks, 
				 const kbus_msg_t *msg);

/** Send a message and get its message-id back.
 *
 * This is the function you probably wanted to use.
 *
 * @param[in]   ks   The ksock to send the message with.
 * @param[in]   msg  The message to send.
 * @param[out]  msg_id On success, filled in with the message id for
 *                      the sent message.
 * @return 0 on success, < 0 on error and set errno.
 */
int   kbus_ksock_send_msg       (kbus_ksock_t ks, 
				 const kbus_msg_t *msg, 
				 struct kbus_msg_id *msg_id);

/** Send a message written by kbus_ksock_write_msg()
 *
 * You probably wanted kbus_ksock_send_msg() instead.
 *
 * @param[in]  ks   The ksock to send the message with.
 * @param[out] msg_id On success, the message-id of the message we sent.
 * @return 0 on success, < 0 on failure and sets errno.
 */
int   kbus_ksock_send           (kbus_ksock_t ks, struct kbus_msg_id *msg_id);


/* Message Functions*/

/** Create a message. We do not take copies of your name and data, until
 *  the message is sent so you may not delete the passed memory until
 *  a successful call to kbus_ksock_send_msg is made. (See 'pointy' 
 *  message in the kbus documentation for more info.)
 *
 * @param[out] msg  on success, points to the created message.
 * @param[in]  name Pointer to the name the message should have.
 * @param[in]  name_len  Length of the message name.
 * @param[in]  data Pointer to some data for the message.
 * @param[in]  data_len  Length of the required message data.
 * @param[in]  flags  KBUS_BIT_XXX
 * @return 0 on success, < 0 and set errno on failure.
 */
int kbus_msg_create            (kbus_msg_t **msg, 
		                const char *name, uint32_t name_len, /* bytes */
                                const void *data, uint32_t data_len, /* bytes */
		                uint32_t flags);

/** Create a short "entire" message. We *do* take copies of your name and data,
 *  you may free it staight away if you want.
 *
 *  "Entire" messages are limited in size (currently to 2048 bytes). That size
 *  includes both the message header and the message data. Thus they are only
 *  suitable for "short" messages.
 *
 *  Unless you really, really need the "copying the strings" functionality, and
 *  are guaranteed to be sending short enough messages, please do not use this
 *  funcion, use kbus_msg_create instead.
 *
 * @param[out] msg  on success, points to the created entire message.
 * @param[in]  name Pointer to the name the message should have.
 * @param[in]  name_len  Length of the message name.
 * @param[in]  data Pointer to some data for the message.
 * @param[in]  data_len  Length of the required message data.
 * @param[in]  flags  KBUS_BIT_XXX
 * @return 0 on success, < 0 and set errno on failure.
 */
int kbus_msg_create_short(kbus_msg_t **msg, 
			  const char *name, uint32_t name_len,/*bytes*/
			  const void *data, uint32_t data_len,/*bytes*/
			  uint32_t flags);

/** Create a reply to a kbus message.  Behaves like kbus_msg_create, 
 *  but the message name is taken from the original (in_reply_to)
 *  message, and the new message's fields are set correctly for a reply.
 *  We do not take copies of your name and data, until the message is sent
 *  so you may not delete the passed memory until a successful call to
 *  kbus_ksock_send_msg is made. (See 'pointy' message in the kbus
 *  documentation for more info.)
 *
 * @param[out] msg  on success, points to the created pointy message.
 * @param[in]  in_reply_to The message that we are creating a reply
 * to. This must be a Request to which we are the Replier (i.e., it
 * tests true to ``kbus_msg_wants_us_to_reply()``.
 * @param[in]  data Pointer to some data for the message.
 * @param[in]  data_len  Length of the required message data.
 * @param[in]  flags  KBUS_BIT_XXX
 * @return 0 on success, < 0 and set errno on failure.
 */
int kbus_msg_create_reply      (kbus_msg_t **msg, 
			        const kbus_msg_t *in_reply_to,
	                        const void *data, uint32_t data_len,
			        uint32_t flags);

/** Create a short "entire" reply to a kbus message. Behaves like 
 *  kbus_msg_create_reply but returns a pointer to an entire message.
 *
 *  In this case we *do* take copies for your name and data, free them
 *  at your leisure. 
 *
 *  "Entire" messages are limited in size (currently to 2048 bytes). That size
 *  includes both the message header and the message data. Thus they are only
 *  suitable for "short" messages, or, in this case, replies.
 *
 *  Unless you really, really need the "copying the strings" functionality, and
 *  are guaranteed to be sending short enough messages, please do not use this
 *  funcion, use kbus_msg_create_reply instead.
 *
 * @param[out] msg  on success, points to the created entire message.
 * @param[in]  in_reply_to The message that we are creating a reply
 * to. This must be a Request to which we are the Replier (i.e., it
 * tests true to ``kbus_msg_wants_us_to_reply()``.
 * @param[in]  data Pointer to some data for the message.
 * @param[in]  data_len  Length of the required message data.
 * @param[in]  flags  KBUS_BIT_XXX
 * @return 0 on success, < 0 and set errno on failure.
 */
int kbus_msg_create_short_reply(kbus_msg_t **msg, 
				const kbus_msg_t *in_reply_to,
				const void *data, 
				uint32_t data_len, /* bytes */
				uint32_t flags);

/** Create a stateful request.
 *
 * A stateful request will only succeed if the Replier identified in
 * the 'to' (and possibly 'final_to') fields is actually the Replier
 * that the message would be delivered to. Thus if the original Replier
 * (the one we think we're sending to) unbinds, and another Ksock binds
 * as Replier to the same message name, delivery will fail.
 *
 * The 'earlier_msg' must be either a Reply from the Replier we want
 * to insist on, or an earlier stateful Request to the Replier we
 * want. The 'to' and 'final_to' fields in the new message are copied
 * appropriately from the 'earlier_msg'.
 *
 * We do not take copies of your name and data until the message is sent
 * so you may not delete the passed memory until a successful call to
 * kbus_ksock_send_msg is made. (See 'pointy' message in the kbus
 * documentation for more info.)
 *
 * @param[out] msg  on success, points to the created pointy message.
 * @param[in]  earlier_msg The Reply or Stateful Request message that we are
 * using to determine who this new message is to.
 * @param[in]  name The name of this new message.
 * @param[in]  name_len The length of the name of this new message.
 * @param[in]  data Pointer to some data for the message.
 * @param[in]  data_len  Length of the required message data.
 * @param[in]  flags  KBUS_BIT_XXX
 * @return 0 on success, < 0 and set errno on failure.
 */
int kbus_msg_create_stateful_request(kbus_msg_t         **msg, 
                                     const kbus_msg_t    *earlier_msg,
                                     const char          *name,
                                     uint32_t             name_len,
                                     const void          *data, 
                                     uint32_t             data_len, /* bytes */
                                     uint32_t             flags);


/** Create an "entire" stateful request.
 *
 * See the non-short version of this call for what it does, and the other
 * "short" message creation calls for why you shouldn't use it, and what
 * the caveats are.
 *
 * @param[out] msg  on success, points to the created pointy message.
 * @param[in]  earlier_msg The Reply or Stateful Request message that we are
 * using to determine who this new message is to.
 * @param[in]  name The name of this new message.
 * @param[in]  name_len The length of the name of this new message.
 * @param[in]  data Pointer to some data for the message.
 * @param[in]  data_len  Length of the required message data.
 * @param[in]  flags  KBUS_BIT_XXX
 * @return 0 on success, < 0 and set errno on failure.
 */
int kbus_msg_create_short_stateful_request(kbus_msg_t         **msg, 
					   const kbus_msg_t    *earlier_msg,
					   const char          *name,
					   uint32_t             name_len,
					   const void          *data, 
					   uint32_t             data_len, /* bytes */
					   uint32_t             flags);

/** Get a pointer to the message name in the kbus message.  The data is not
 *  copied so the pointer returned points into the middle of the data in msg.
 *
 * @param[in]  msg Pointer to a kbus message.
 * @param[out] name Points to the messsage name
 */
int kbus_msg_name_ptr          (const kbus_msg_t *msg, 
				char **name);

/** Get a pointer to the message data in the kbus message.  The data is not
 *  copied so the pointer returned points into the middle of the data in msg.
 *
 * @param[in]  msg Pointer to a kbus message.
 * @param[out] name Points to the messsage name
 */
int kbus_msg_data_ptr          (const kbus_msg_t *msg, 
				void **data);


/** Dispose of a message, releasing all associated memory.
 *
 * @param[inout] kms_p  Pointer to the message header to destroy.
 */
void kbus_msg_dispose          (kbus_msg_t **kms_p);

/**  Dump a message to stdout.
 *
 * @param[in] msg  KBus message to dump
 * @param[in] dump_data Dump the data too?
 */
void kbus_msg_dump             (const kbus_msg_t *msg, 
				int dump_data);

/**  Find out the size of a kbus message include any name/data in an
 *   entire message. 
 *
 * @param[in] msg  KBus message
 * @return size in bytes.
 */
int kbus_msg_sizeof            (const kbus_msg_t *msg);


/** Compare two kbus message ids and return < 0 if a < b, 0 if a==b, 1 if a > b
  */
static inline int kbus_id_cmp  (const struct kbus_msg_id *a, 
				const struct kbus_msg_id *b)
{
  if (!a && !b) { return 0; }
  if (!a) { return -1; }
  if (!b) { return 1; }

  const uint32_t a_net = a->network_id;
  const uint32_t b_net = b->network_id;

  if (a_net < b_net)
    {
      return -1;
    }
  else if (a_net > b_net)
    {
      return 1;
    }
  
  const uint32_t a_serial = a->serial_num;
  const uint32_t b_serial = b->serial_num;

  if (a_serial < b_serial)
    {
      return -1;
    }
  if (a_serial > b_serial)
    {
      return 1;
    }

  return 0;
}

/** Request the KBUS kernel module to create a new device (/dev/kbus<n>).
 *
 * Note that it takes the kernel's hotplugging mechanisms a little while to
 * notice/activate the device, so do not expect it to be available immediately
 * on return.
 *
 * @param[in]  ks          The ksock to use as a means of communicating with KBUS.
 * @param[out] new_device  On success, the new device number (<n>).
 * @return 0 on success , <0 setting errno on failure.
 */
int   kbus_new_device(kbus_ksock_t ks, uint32_t *device_number);

/** Determine if a KBUS message is a Reply.
 *
 * This is a convenience function, which checks the message to see
 * if it has the 'in_reply_to' field set. If it does, then it is a Reply.
 *
 * It returns true if the message is a Reply.
 */
int kbus_msg_is_reply(const kbus_msg_t   *msg);

/** Determine if a KBUS message is a Request
 *
 * This is a convenience function, which checks the message flags
 * to see if it has the WANT_A_REPLY flag set.
 *
 * (Note that stateful requests are also requests!)
 *
 * It returns true if the message is a Request.
 */
int kbus_msg_is_request(const kbus_msg_t  *msg);

/** Determine if a KBUS message is a Stateful Request
 *
 * This is a convenience function, which checks the message flags
 * to see if it has the WANT_A_REPLY flag set, and then checks
 * to see if the 'to' field is set.
 *
 * It returns true if the message is a Stateful Request.
 */
int kbus_msg_is_stateful_request(const kbus_msg_t  *msg);

/** Determine if a KBUS Request message wants us (this Ksock) to reply.
 *
 * This is a convenience function, which checks the message flags
 * to see if it has the WANT_A_REPLY and WANT_YOU_TO_REPLY flags set.
 *
 * It returns true if the message is a Request which we (this Ksock)
 * should Reply to. It returns false if this is not a Request, or if
 * it is a (copy of a) Request which we have received because we are
 * bound as Listener to this message name.
 */
int kbus_msg_wants_us_to_reply(const kbus_msg_t  *msg);

#ifdef __cplusplus
}
#endif 

#endif /* _LKBUS_H_INCLUDED_ */
