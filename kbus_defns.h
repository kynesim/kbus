/* Kbus kernel module external headers
 *
 * This file provides the definitions (datastructures and ioctls) needed to
 * communicate with the KBUS character device driver.
 */

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
 *   Tony Ibbs <tony.ibbs@gmail.com>
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

#ifndef _kbus_defns
#define _kbus_defns

#if ! __KERNEL__ && defined(__cplusplus)
extern "C" {
#endif

#if __KERNEL__
#include <linux/kernel.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

/* A message id is made up of two fields.
 *
 * If the network id is 0, then it is up to us (KBUS) to assign the
 * serial number. In other words, this is a local message.
 *
 * If the network id is non-zero, then this message is presumed to
 * have originated from another "network", and we preserve both the
 * network id and the serial number.
 *
 * The message id {0,0} is special and reserved (for use by KBUS).
 */
struct kbus_msg_id {
	uint32_t		network_id;
	uint32_t		serial_num;
};

/* When the user asks to bind a message name to an interface, they use: */
struct kbus_bind_request {
	uint32_t	 is_replier;	/* are we a replier? */
	uint32_t	 name_len;
	char		*name;
};

/* When the user requests the id of the replier to a message, they use: */
struct kbus_bind_query {
	uint32_t	 return_id;
	uint32_t	 name_len;
	char		*name;
};

/* When the user writes/reads a message, they use: */
struct kbus_message_header {
	/*
	 * The guards
	 * ----------
	 *
	 * * 'start_guard' is notionally "kbus", and 'end_guard' (the 32 bit
	 *   word after the rest of the message datastructure) is notionally
	 *   "subk". Obviously that depends on how one looks at the 32-bit
	 *   word. Every message datastructure shall start with a start guard
	 *   and end with an end guard.
	 *
	 * These provide some help in checking that a message is well formed,
	 * and in particular the end guard helps to check for broken length
	 * fields.
	 *
	 * - 'id' identifies this particular message.
	 *
	 *   When writing a new message, set this to 0 (actually, KBUS will
	 *   entirely ignore this field, as it set it itself).
	 *
	 *   When reading a message, this will have been set by KBUS.
	 *
	 *   When replying to a message, copy this value into the 'in_reply_to'
	 *   field, so that the recipient will know what message this was a
	 *   reply to.
	 *
	 * - 'in_reply_to' identifies the message this is a reply to.
	 *
	 *   This shall be set to 0 unless this message *is* a reply to a
	 *   previous message. In other words, if this value is non-0, then
	 *   the message *is* a reply.
	 *
	 * - 'to' is who the message is to be sent to.
	 *
	 *   When writing a new message, this should normally be set to 0,
	 *   meaning "anyone listening" (but see below if "state" is being
	 *   maintained).
	 *
	 *   When replying to a message, it shall be set to the 'from' value
	 *   of the orginal message.
	 *
	 *   When constructing a request message (a message wanting a reply),
	 *   then it can be set to a specific listener id. When such a message
	 *   is sent, if the replier bound (at that time) does not have that
	 *   specific listener id, then the send will fail.
	 *
	 * - 'from' indicates who sent the message.
	 *
	 *   When writing a new message, set this to 0.
	 *
	 *   When reading a message, this will have been set by KBUS.
	 *
	 *   When replying to a message, put the value read into 'to',
	 *   and set 'from' to 0 (see the "hmm" caveat under 'to' above,
	 *   though).
	 *
	 *       Question: if a reply is not wanted, should this be 0?
	 *
	 * - 'flags' indicates the type of message.
	 *
	 *   When writing a message, this can be used to indicate that
	 *
	 *   * the message is URGENT
	 *   * a reply is wanted
	 *
	 *   When reading a message, this indicates:
	 *
	 *   * the message is URGENT
	 *   * a reply is wanted
	 *
	 *   When writing a reply, set this field to 0 (I think).
	 *
	 * - 'name_len' is the length of the message name in bytes.
	 *
	 *   This must be non-zero.
	 *
	 * - 'data_len' is the length of the message data in bytes. It may be
	 *   zero if there is no data.
	 *
	 * - 'name' is a pointer to the message name. This should be null
	 *   terminated, as is the normal case for C strings.
	 *
	 *   NB: If this is zero, then the name will be present, but after
	 *   the end of this datastructure, and padded out to a multiple of
	 *   four bytes (see kbus_entire_message). When doing this padding,
	 *   remember to allow for the terminating null byte. If this field is
	 *   zero, then 'data' shall also be zero.
	 *
	 * - 'data' is a pointer to the data. If there is no data (if
	 *   'data_len' is zero), then this shall also be zero. The data is
	 *   not touched by KBUS, and may include any values.
	 *
	 *   NB: If this is zero, then the data will occur immediately
	 *   after the message name, padded out to a multiple of four bytes.
	 *   See the note for 'name' above.
	 *
	 */
	uint32_t		 start_guard;
	struct kbus_msg_id	 id;	      /* Unique to this message */
	struct kbus_msg_id	 in_reply_to; /* Which message this is a reply to */
	uint32_t		 to;	      /* 0 (normally) or a replier id */
	uint32_t		 from;	      /* 0 (KBUS) or the sender's id */
	uint32_t		 flags;	      /* Message type/flags */
	uint32_t		 name_len;    /* Message name's length, in bytes */
	uint32_t		 data_len;    /* Message length, also in bytes */
	char			*name;
	void			*data;
	uint32_t		 end_guard;
};

#define KBUS_MSG_START_GUARD	0x7375626B
#define KBUS_MSG_END_GUARD	0x6B627573

/*
 * When a message is returned by 'read', it is actually returned using the
 * following datastructure, in which:
 *
 * - 'header.name' will point to 'rest[0]'
 * - 'header.data' will point to 'rest[(header.name_len+3)/4]'
 *
 * Followed by the name (padded to 4 bytes, remembering to allow for the
 * terminating null byte), followed by the data (padded to 4 bytes) followed by
 * (another) end_guard.
 */
struct kbus_entire_message {
	struct	kbus_message_header	header;
	uint32_t			rest[];
};

/*
 * We have a couple of arbitrary limits:
 *
 * 1. A message name may not be more than 1000 characters long
 *    (some limit seems sensible, after all)
 *
 * 2. An "entire" message, when written to us, may not be more than
 *    2000 bytes long -- this guarantees we're not going to hit any
 *    page boundaries when trying to copy the entire message into
 *    kernel space. If big messages are wanted, write them with
 *    "pointy" headers.
 */
#define KBUS_MAX_NAME_LEN	1000
#define KBUS_MAX_ENTIRE_LEN	2048

/*
 * The length (in bytes) of the name after padding, allowing for a terminating
 * null byte.
 */
#define KBUS_PADDED_NAME_LEN(name_len)   (4 * ((name_len + 1 + 3) / 4))

/*
 * The length (in bytes) of the data after padding
 */
#define KBUS_PADDED_DATA_LEN(data_len)   (4 * ((data_len + 3) / 4))

/*
 * Given name_len (in bytes) and data_len (in bytes), return the
 * length of the appropriate kbus_entire_message_struct, in bytes
 *
 * Note that we're allowing for a zero byte after the end of the message name.
 *
 * Remember that "sizeof" doesn't count the 'rest' field in our message
 * structure.
 */
#define KBUS_ENTIRE_MSG_LEN(name_len,data_len)    \
	(sizeof(struct kbus_entire_message) + \
	 KBUS_PADDED_NAME_LEN(name_len) + \
	 KBUS_PADDED_DATA_LEN(data_len) + 4)

/*
 * The message name starts at entire->rest[0].
 * The message data starts after the message name - given the message
 * name's length (in bytes), that is at index:
 */
#define KBUS_ENTIRE_MSG_DATA_INDEX(name_len)     ((name_len+1+3)/4)
/*
 * Given the message name length (in bytes) and the message data length (also
 * in bytes), the index of the entire message end guard is thus:
 */
#define KBUS_ENTIRE_MSG_END_GUARD_INDEX(name_len,data_len)  ((name_len+1+3)/4 + (data_len+3)/4)

/*
 * Find a pointer to the name.
 *
 * It's either the given name pointer, or just after the header (if the pointer
 * is NULL)
 */
static inline char *kbus_name_ptr(const struct kbus_message_header  *hdr)
{
	if (hdr->name) {
		return hdr->name;
	} else {
		struct kbus_entire_message	*entire;
		entire = (struct kbus_entire_message *)hdr;
		return (char *) &entire->rest[0];
	}
}

/*
 * Find a pointer to the data.
 *
 * It's either the given data pointer, or just after the name (if the pointer
 * is NULL)
 */
static inline void *kbus_data_ptr(const struct kbus_message_header  *hdr)
{
	if (hdr->data) {
		return hdr->data;
	} else {
		struct kbus_entire_message	*entire;
		uint32_t			 data_idx;

		entire = (struct kbus_entire_message *)hdr;
		data_idx = KBUS_ENTIRE_MSG_DATA_INDEX(hdr->name_len);
		return (void *) &entire->rest[data_idx];
	}
}

/*
 * Find a pointer to the (second/final) end guard.
 */
static inline uint32_t *kbus_end_ptr(struct kbus_entire_message  *entire)
{
	uint32_t	end_guard_idx;
	end_guard_idx = KBUS_ENTIRE_MSG_END_GUARD_INDEX(entire->header.name_len,
							entire->header.data_len);
	return (uint32_t *) &entire->rest[end_guard_idx];
}

/*
 * Things KBUS changes in a message
 * --------------------------------
 * In general, KBUS leaves the content of a message alone. However, it does
 * change:
 *
 * - the message id (if id.network_id is unset)
 * - the from id (to indicate the KSock this message was sent from)
 * - the KBUS_BIT_WANT_YOU_TO_REPLY bit in the flags (set or cleared
 *   as appropriate)
 * - the SYNTHETIC bit, which KBUS will always unset in a user message
 */

/*
 * Flags for the message 'flags' word
 * ----------------------------------
 * The KBUS_BIT_WANT_A_REPLY bit is set by the sender to indicate that a
 * reply is wanted. This makes the message into a request.
 *
 *     Note that setting the WANT_A_REPLY bit (i.e., a request) and
 *     setting 'in_reply_to' (i.e., a reply) is bound to lead to
 *     confusion, and the results are undefined (i.e., don't do it).
 *
 * The KBUS_BIT_WANT_YOU_TO_REPLY bit is set by KBUS on a particular message
 * to indicate that the particular recipient is responsible for replying
 * to (this instance of the) message. Otherwise, KBUS clears it.
 *
 * The KBUS_BIT_SYNTHETIC bit is set by KBUS when it generates a synthetic
 * message (an exception, if you will), for instance when a replier has
 * gone away and therefore a reply will never be generated for a request
 * that has already been queued.
 *
 *     Note that KBUS does not check that a sender has not set this
 *     on a message, but doing so may lead to confusion.
 *
 * The KBUS_BIT_URGENT bit is set by the sender if this message is to be
 * treated as urgent - i.e., it should be added to the *front* of the
 * recipient's message queue, not the back.
 *
 * Send flags
 * ==========
 * There are two "send" flags, KBUS_BIT_ALL_OR_WAIT and KBUS_BIT_ALL_OR_FAIL.
 * Either one may be set, or both may be unset.
 *
 *    If both bits are set, the message will be rejected as invalid.
 *
 *    Both flags are ignored in reply messages (i.e., messages with the
 *    'in_reply_to' field set).
 *
 * If both are unset, then a send will behave in the default manner. That is,
 * the message will be added to a listener's queue if there is room but
 * otherwise the listener will (silently) not receive the message.
 *
 *     (Obviously, if the listener is a replier, and the message is a request,
 *     then a KBUS message will be synthesised in the normal manner when a
 *     request is lost.)
 *
 * If the KBUS_BIT_ALL_OR_WAIT bit is set, then a send should block until
 * all recipients can be sent the message. Specifically, before the message is
 * sent, all recipients must have room on their message queues for this
 * message, and if they do not, the send will block until there is room for the
 * message on all the queues.
 *
 * If the KBUS_BIT_ALL_OR_FAIL bit is set, then a send should fail if all
 * recipients cannot be sent the message. Specifically, before the message is
 * sent, all recipients must have room on their message queues for this
 * message, and if they do not, the send will fail.
 */

#if ! __KERNEL__
#define BIT(num)                 (((unsigned)1) << (num))
#endif

#define	KBUS_BIT_WANT_A_REPLY		BIT(0)
#define KBUS_BIT_WANT_YOU_TO_REPLY	BIT(1)
#define KBUS_BIT_SYNTHETIC		BIT(2)
#define KBUS_BIT_URGENT			BIT(3)

#define KBUS_BIT_ALL_OR_WAIT		BIT(8)
#define KBUS_BIT_ALL_OR_FAIL		BIT(9)




#define KBUS_IOC_MAGIC	'k'	/* 0x6b - which seems fair enough for now */
/*
 * RESET: reserved for future use
 */
#define KBUS_IOC_RESET	  _IO(  KBUS_IOC_MAGIC,  1)
/*
 * BIND - bind a KSock to a message name
 * arg: struct kbus_bind_request, indicating what to bind to
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_BIND	  _IOW( KBUS_IOC_MAGIC,  2, char *)
/*
 * UNBIND - unbind a KSock from a message id
 * arg: struct kbus_bind_request, indicating what to unbind from
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_UNBIND	  _IOW( KBUS_IOC_MAGIC,  3, char *)
/*
 * KSOCKID - determine a KSock's KSock id
 * arg (out): uint32_t, indicating this KSock's internal id
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_KSOCKID  _IOR( KBUS_IOC_MAGIC,  4, char *)
/*
 * REPLIER - determine the KSock id of the replier for a message name
 * arg: struct kbus_bind_query
 *
 *    - on input, specify the message name and replier flag to ask about
 *    - on output, KBUS fills in the relevant KSock id in the return_value,
 *      or 0 if there is no bound replier
 *
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_REPLIER  _IOWR(KBUS_IOC_MAGIC,  5, char *)
/*
 * NEXTMSG - pop the next message from the read queue
 * arg (out): uint32_t, number of bytes in the next message, 0 if there is no
 *            next message
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_NEXTMSG  _IOR( KBUS_IOC_MAGIC,  6, char *)
/*
 * LENLEFT - determine how many bytes are left to read of the current message
 * arg (out): uint32_t, number of bytes left, 0 if there is no current read
 *            message
 * retval: 1 if there was a message, 0 if there wasn't, negative for failure
 */
#define KBUS_IOC_LENLEFT  _IOR( KBUS_IOC_MAGIC,  7, char *)
/*
 * SEND - send the current message
 * arg (out): struct kbus_msg_id, the message id of the sent message
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_SEND	  _IOR( KBUS_IOC_MAGIC,  8, char *)
/*
 * DISCARD - discard the message currently being written (if any)
 * arg: none
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_DISCARD  _IO(  KBUS_IOC_MAGIC,  9)
/*
 * LASTSENT - determine the message id of the last message SENT
 * arg (out): struct kbus_msg_id, {0,0} if there was no last message
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_LASTSENT _IOR( KBUS_IOC_MAGIC, 10, char *)
/*
 * MAXMSGS - set the maximum number of messages on a KSock read queue
 * arg (in): uint32_t, the requested length of the read queue, or 0 to just
 *           request how many there are
 * arg (out): uint32_t, the length of the read queue after this call has
 *            succeeded
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_MAXMSGS  _IOWR(KBUS_IOC_MAGIC, 11, char *)
/*
 * NUMMSGS - determine how many messages are in the read queue for this KSock
 * arg (out): uint32_t, the number of messages in the read queue.
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_NUMMSGS  _IOR( KBUS_IOC_MAGIC, 12, char *)
/*
 * UNREPLIEDTO - determine the number of requests (marked "WANT_YOU_TO_REPLY")
 * which we still need to reply to.
 * arg(out): uint32_t, said number
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_UNREPLIEDTO  _IOR( KBUS_IOC_MAGIC, 13, char *)
/*
 * MSGONLYONCE - should we receive a message only once?
 *
 * This IOCTL tells a KSock whether it should only receive a particular message
 * once, even if it is both a Replier and Listener for the message (in which
 * case it will always get the message as Replier, if appropriate), or if it is
 * registered as multiple Listeners for the message.
 *
 * arg(in): uint32_t, 1 to change to "only once", 0 to change to the default,
 * 0xFFFFFFFF to just return the current/previous state.
 * arg(out): uint32_t, the previous state.
 * retval: 0 for success, negative for failure (-EINVAL if arg in was not one
 * of the specified values)
 */
#define KBUS_IOC_MSGONLYONCE  _IOR( KBUS_IOC_MAGIC, 14, char *)
/*
 * VERBOSE - should KBUS output verbose "printk" messages (for this device)?
 *
 * This IOCTL tells a KSock whether it should output debugging messages. It is
 * only effective if the kernel module has been built with the VERBOSE_DEBUGGING
 * flag set.
 *
 * arg(in): uint32_t, 1 to change to "verbose", 0 to change to "quiet",
 * 0xFFFFFFFF to just return the current/previous state.
 * arg(out): uint32_t, the previous state.
 * retval: 0 for success, negative for failure (-EINVAL if arg in was not one
 * of the specified values)
 */
#define KBUS_IOC_VERBOSE  _IOR( KBUS_IOC_MAGIC, 15, char *)

/* XXX If adding another IOCTL, remember to increment the next number! XXX */
#define KBUS_IOC_MAXNR	15

#if ! __KERNEL__ && defined(__cplusplus)
}
#endif

#endif // _kbus_defns

// Kernel style layout -- note that including this text contravenes the Linux
// coding style, and is thus a Bad Thing. Expect these lines to be removed if
// this ever gets added to the kernel distribution.
// Local Variables:
// c-set-style: "linux"
// End:
// vim: set tabstop=8 shiftwidth=8 noexpandtab:
