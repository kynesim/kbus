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
 *   Tony Ibbs <tibs@tonyibbs.co.uk>
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

#if !__KERNEL__ && defined(__cplusplus)
extern "C" {
#endif

#if __KERNEL__
#include <linux/kernel.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

/*
 * A message id is made up of two fields.
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
	__u32 network_id;
	__u32 serial_num;
};

/*
 * kbus_orig_from is used for the "originally from" and "finally to" ids
 * in the message header. These in turn are used when messages are
 * being sent between KBUS systems (via KBUS "Limpets"). KBUS the kernel
 * module transmits them, unaltered, but does not use them (although
 * debug messages may report them).
 *
 * An "originally from" or "finally to" id is made up of two fields, the
 * network id (which indicates the Limpet, if any, that originally gated the
 * message), and a local id, which is the Ksock id of the original sender
 * of the message, on its local KBUS.
 *
 * If the network id is 0, then the "originally from" id is not being used.
 *
 * Limpets and these fields are discussed in more detail in the userspace
 * KBUS documentation - see http://kbus-messaging.org/ for pointers to
 * more information.
 */
struct kbus_orig_from {
	__u32 network_id;
	__u32 local_id;
};

/* When the user asks to bind a message name to an interface, they use: */
struct kbus_bind_request {
	__u32 is_replier;	/* are we a replier? */
	__u32 name_len;
	char *name;
};

/* When the user requests the id of the replier to a message, they use: */
struct kbus_bind_query {
	__u32 return_id;
	__u32 name_len;
	char *name;
};

/* When the user writes/reads a message, they use: */
struct kbus_message_header {
	/*
	 * The guards
	 * ----------
	 *
	 * * 'start_guard' is notionally "Kbus", and 'end_guard' (the 32 bit
	 *   word after the rest of the message datastructure) is notionally
	 *   "subK". Obviously that depends on how one looks at the 32-bit
	 *   word. Every message datastructure shall start with a start guard
	 *   and end with an end guard.
	 *
	 * These provide some help in checking that a message is well formed,
	 * and in particular the end guard helps to check for broken length
	 * fields.
	 *
	 * - 'id' identifies this particular message.
	 *
	 *   When a user writes a new message, they should set this to {0,0}.
	 *   KBUS will then set a new message id for the message.
	 *
	 *   When a user reads a message, this will have been set by KBUS.
	 *
	 *   When a user replies to a message, they should copy this value
	 *   into the 'in_reply_to' field, so that the recipient will know
	 *   what message this was a reply to.
	 *
	 * - 'in_reply_to' identifies the message this is a reply to.
	 *
	 *   This shall be set to {0,0} unless this message *is* a reply to a
	 *   previous message. In other words, if this value is non-0, then
	 *   the message *is* a reply.
	 *
	 * - 'to' is who the message is to be sent to.
	 *
	 *   When a user writes a new message, this should normally be set
	 *   to {0,0}, meaning "anyone listening" (but see below if "state"
	 *   is being maintained).
	 *
	 *   When replying to a message, it shall be set to the 'from' value
	 *   of the orginal message.
	 *
	 *   When constructing a request message (a message wanting a reply),
	 *   the user can set it to a specific replier id, to produce a stateful
	 *   request. This is normally done by copying the 'from' of a previous
	 *   Reply from the appropriate replier. When such a message is sent,
	 *   if the replier bound (at that time) does not have that specific
	 *   id, then the send will fail.
	 *
	 *   Note that if 'to' is set, then 'orig_from' should also be set.
	 *
	 * - 'from' indicates who sent the message.
	 *
	 *   When a user is writing a new message, they should set this
	 *   to {0,0}.
	 *
	 *   When a user is reading a message, this will have been set
	 *   by KBUS.
	 *
	 *   When a user replies to a message, the reply should have its
	 *   'to' set to the original messages 'from', and its 'from' set
	 *   to {0,0} (see the "hmm" caveat under 'to' above, though).
	 *
	 * - 'orig_from' and 'final_to' are used when Limpets are mediating
	 *   KBUS messages between KBUS devices (possibly on different
	 *   machines). See the description by the datastructure definition
	 *   above. The KBUS kernel preserves and propagates their values,
	 *   but does not alter or use them.
	 *
	 * - 'extra' is currently unused, and KBUS will set it to zero.
	 *   Future versions of KBUS may treat it differently.
	 *
	 * - 'flags' indicates the type of message.
	 *
	 *   When a user writes a message, this can be used to indicate
	 *   that:
	 *
	 *   * the message is URGENT
	 *   * a reply is wanted
	 *
	 *   When a user reads a message, this indicates if:
	 *
	 *   * the message is URGENT
	 *   * a reply is wanted
	 *
	 *   When a user writes a reply, this field should be set to 0.
	 *
	 *   The top half of the 'flags' is not touched by KBUS, and may
	 *   be used for any purpose the user wishes.
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
	__u32 start_guard;
	struct kbus_msg_id id;	/* Unique to this message */
	struct kbus_msg_id in_reply_to;	/* Which message this is a reply to */
	__u32 to;		/* 0 (empty) or a replier id */
	__u32 from;		/* 0 (KBUS) or the sender's id */
	struct kbus_orig_from orig_from;/* Cross-network linkage */
	struct kbus_orig_from final_to;	/* Cross-network linkage */
	__u32 extra;	/* ignored field - future proofing */
	__u32 flags;	/* Message type/flags */
	__u32 name_len;	/* Message name's length, in bytes */
	__u32 data_len;	/* Message length, also in bytes */
	char *name;
	void *data;
	__u32 end_guard;
};

#define KBUS_MSG_START_GUARD	0x7375624B
#define KBUS_MSG_END_GUARD	0x4B627573

/*
 * When a message is returned by 'read', it is actually returned using the
 * following datastructure, in which:
 *
 * - 'header.name' will point to 'rest[0]'
 * - 'header.data' will point to 'rest[(header.name_len+3)/4]'
 *
 * followed by the name (padded to 4 bytes, remembering to allow for the
 * terminating null byte), followed by the data (padded to 4 bytes) followed by
 * (another) end_guard.
 */
struct kbus_entire_message {
	struct kbus_message_header header;
	__u32 rest[];
};

/*
 * We limit a message name to at most 1000 characters (some limit seems
 * sensible, after all)
 */
#define KBUS_MAX_NAME_LEN	1000

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
#define KBUS_ENTIRE_MSG_LEN(name_len, data_len)    \
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
#define KBUS_ENTIRE_MSG_END_GUARD_INDEX(name_len, data_len)  \
	((name_len+1+3)/4 + (data_len+3)/4)

/*
 * Find a pointer to the message's name.
 *
 * It's either the given name pointer, or just after the header (if the pointer
 * is NULL)
 */
static inline char *kbus_msg_name_ptr(const struct kbus_message_header
				      *hdr)
{
	if (hdr->name) {
		return hdr->name;
	} else {
		struct kbus_entire_message *entire;
		entire = (struct kbus_entire_message *)hdr;
		return (char *)&entire->rest[0];
	}
}

/*
 * Find a pointer to the message's data.
 *
 * It's either the given data pointer, or just after the name (if the pointer
 * is NULL)
 */
static inline void *kbus_msg_data_ptr(const struct kbus_message_header
				      *hdr)
{
	if (hdr->data) {
		return hdr->data;
	} else {
		struct kbus_entire_message *entire;
		__u32 data_idx;

		entire = (struct kbus_entire_message *)hdr;
		data_idx = KBUS_ENTIRE_MSG_DATA_INDEX(hdr->name_len);
		return (void *)&entire->rest[data_idx];
	}
}

/*
 * Find a pointer to the message's (second/final) end guard.
 */
static inline __u32 *kbus_msg_end_ptr(struct kbus_entire_message
					 *entire)
{
	__u32 end_guard_idx =
		KBUS_ENTIRE_MSG_END_GUARD_INDEX(entire->header.name_len,
						entire->header.data_len);
	return (__u32 *) &entire->rest[end_guard_idx];
}

/*
 * Things KBUS changes in a message
 * --------------------------------
 * In general, KBUS leaves the content of a message alone. However, it does
 * change:
 *
 * - the message id (if id.network_id is unset - it assigns a new serial
 *   number unique to this message)
 * - the from id (if from.network_id is unset - it sets the local_id to
 *   indicate the Ksock this message was sent from)
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

/*
 * When a $.KBUS.ReplierBindEvent message is constructed, we use the
 * following to encapsulate its data.
 *
 * This indicates whether it is a bind or unbind event, who is doing the
 * bind or unbind, and for what message name. The message name is padded
 * out to a multiple of four bytes, allowing for a terminating null byte,
 * but the name length is the length without said padding (so, in C terms,
 * strlen(name)).
 *
 * As for the message header data structure, the actual data "goes off the end"
 * of the datastructure.
 */
struct kbus_replier_bind_event_data {
	__u32 is_bind;	/* 1=bind, 0=unbind */
	__u32 binder;	/* Ksock id of binder */
	__u32 name_len;	/* Length of name */
	__u32 rest[];	/* Message name */
};

#if !__KERNEL__
#define BIT(num)                 (((unsigned)1) << (num))
#endif

#define	KBUS_BIT_WANT_A_REPLY		BIT(0)
#define KBUS_BIT_WANT_YOU_TO_REPLY	BIT(1)
#define KBUS_BIT_SYNTHETIC		BIT(2)
#define KBUS_BIT_URGENT			BIT(3)

#define KBUS_BIT_ALL_OR_WAIT		BIT(8)
#define KBUS_BIT_ALL_OR_FAIL		BIT(9)

/*
 * Standard message names
 * ======================
 * KBUS itself has some predefined message names.
 *
 * Synthetic Replies with no data
 * ------------------------------
 * These are sent to the original Sender of a Request when KBUS knows that the
 * Replier is not going to Reply. In all cases, you can identify which message
 * they concern by looking at the "in_reply_to" field:
 *
 * * Replier.GoneAway - the Replier has gone away before reading the Request.
 * * Replier.Ignored - the Replier has gone away after reading a Request, but
 *   before replying to it.
 * * Replier.Unbound - the Replier has unbound (as Replier) from the message
 *   name, and is thus not going to reply to this Request in its unread message
 *   queue.
 * * Replier.Disappeared - the Replier has disappeared when an attempt is made
 *   to send a Request whilst polling (i.e., after EAGAIN was returned from an
 *   earlier attempt to send a message). This typically means that the Ksock
 *   bound as Replier closed.
 * * ErrorSending - an unexpected error occurred when trying to send a Request
 *   to its Replier whilst polling.
 */
#define KBUS_MSG_NAME_REPLIER_GONEAWAY		"$.KBUS.Replier.GoneAway"
#define KBUS_MSG_NAME_REPLIER_IGNORED		"$.KBUS.Replier.Ignored"
#define KBUS_MSG_NAME_REPLIER_UNBOUND		"$.KBUS.Replier.Unbound"
#define KBUS_MSG_NAME_REPLIER_DISAPPEARED	"$.KBUS.Replier.Disappeared"
#define KBUS_MSG_NAME_ERROR_SENDING		"$.KBUS.ErrorSending"

#define KBUS_IOC_MAGIC	'k'	/* 0x6b - which seems fair enough for now */
/*
 * RESET: reserved for future use
 */
#define KBUS_IOC_RESET	    _IO(KBUS_IOC_MAGIC,  1)
/*
 * BIND - bind a Ksock to a message name
 * arg: struct kbus_bind_request, indicating what to bind to
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_BIND	   _IOW(KBUS_IOC_MAGIC,  2, char *)
/*
 * UNBIND - unbind a Ksock from a message id
 * arg: struct kbus_bind_request, indicating what to unbind from
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_UNBIND	   _IOW(KBUS_IOC_MAGIC,  3, char *)
/*
 * KSOCKID - determine a Ksock's Ksock id
 *
 * The network_id for the current Ksock is, by definition, 0, so we don't need
 * to return it.
 *
 * arg (out): __u32, indicating this Ksock's local_id
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_KSOCKID   _IOR(KBUS_IOC_MAGIC,  4, char *)
/*
 * REPLIER - determine the Ksock id of the replier for a message name
 * arg: struct kbus_bind_query
 *
 *    - on input, specify the message name to ask about.
 *    - on output, KBUS fills in the relevant Ksock id in the return_value,
 *      or 0 if there is no bound replier
 *
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_REPLIER  _IOWR(KBUS_IOC_MAGIC,  5, char *)
/*
 * NEXTMSG - pop the next message from the read queue
 * arg (out): __u32, number of bytes in the next message, 0 if there is no
 *            next message
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_NEXTMSG   _IOR(KBUS_IOC_MAGIC,  6, char *)
/*
 * LENLEFT - determine how many bytes are left to read of the current message
 * arg (out): __u32, number of bytes left, 0 if there is no current read
 *            message
 * retval: 1 if there was a message, 0 if there wasn't, negative for failure
 */
#define KBUS_IOC_LENLEFT   _IOR(KBUS_IOC_MAGIC,  7, char *)
/*
 * SEND - send the current message
 * arg (out): struct kbus_msg_id, the message id of the sent message
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_SEND	   _IOR(KBUS_IOC_MAGIC,  8, char *)
/*
 * DISCARD - discard the message currently being written (if any)
 * arg: none
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_DISCARD    _IO(KBUS_IOC_MAGIC,  9)
/*
 * LASTSENT - determine the message id of the last message SENT
 * arg (out): struct kbus_msg_id, {0,0} if there was no last message
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_LASTSENT  _IOR(KBUS_IOC_MAGIC, 10, char *)
/*
 * MAXMSGS - set the maximum number of messages on a Ksock read queue
 * arg (in): __u32, the requested length of the read queue, or 0 to just
 *           request how many there are
 * arg (out): __u32, the length of the read queue after this call has
 *            succeeded
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_MAXMSGS  _IOWR(KBUS_IOC_MAGIC, 11, char *)
/*
 * NUMMSGS - determine how many messages are in the read queue for this Ksock
 * arg (out): __u32, the number of messages in the read queue.
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_NUMMSGS   _IOR(KBUS_IOC_MAGIC, 12, char *)
/*
 * UNREPLIEDTO - determine the number of requests (marked "WANT_YOU_TO_REPLY")
 * which we still need to reply to.
 * arg(out): __u32, said number
 * retval: 0 for success, negative for failure
 */
#define KBUS_IOC_UNREPLIEDTO _IOR(KBUS_IOC_MAGIC, 13, char *)

/*
 * IOCTL 14 is not used, because it is introduced in the next revision,
 * (obviously, in real history this was done in a different order) and
 * I don't want to alter the number for VERBOSE.
 */

/*
 * VERBOSE - should KBUS output verbose "printk" messages (for this device)?
 *
 * This IOCTL tells a Ksock whether it should output debugging messages. It is
 * only effective if the kernel module has been built with the VERBOSE_DEBUGGING
 * flag set.
 *
 * arg(in): __u32, 1 to change to "verbose", 0 to change to "quiet",
 * 0xFFFFFFFF to just return the current/previous state.
 * arg(out): __u32, the previous state.
 * retval: 0 for success, negative for failure (-EINVAL if arg in was not one
 * of the specified values)
 */
#define KBUS_IOC_VERBOSE  _IOWR(KBUS_IOC_MAGIC, 15, char *)

/* If adding another IOCTL, remember to increment the next number! */
#define KBUS_IOC_MAXNR	15

#if !__KERNEL__ && defined(__cplusplus)
}
#endif

#endif /* _kbus_defns */
