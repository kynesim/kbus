/* KBUS kernel module - internal definitions
 *
 * This is a character device driver, providing the messaging support
 * for KBUS.
 *
 * This header contains the definitions used internally by kbus.c.
 * At the moment nothing else is expected to include this file.
 *
 * KBUS clients should include (at least) kbus_defns.h.
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

#ifndef _kbus_internal
#define _kbus_internal

/*
 * KBUS can support multiple devices, as /dev/kbus<N>. These all have
 * the same major device number, and map to differing minor device
 * numbers. <N> will also be the minor device number, but don't rely
 * on that for anything.
 *
 * When KBUS starts up, it will always setup a single device (/dev/kbus0),
 * but it can be asked to setup more - for instance:
 *
 *     # insmod kbus.ko kbus_num_devices=5
 *
 * There is also an IOCTL to allow user-space to request a new device as
 * necessary. The hot plugging mechanisms should cause the device to appear "as
 * if by magic".
 *
 *     (This last means that we *could* default to setting up zero devices
 *     at module startup, and leave the user to ask for the first one, but
 *     that seems rather cruel.)
 *
 * We need to set a maximum number of KBUS devices (corresponding to a limit on
 * minor device numbers). The obvious limit (corresponding to what we'd have
 * got if we used the deprecated "register_chrdev" to setup our device) is 256,
 * so we'll go with that.
 */
#define KBUS_MIN_NUM_DEVICES		  1

#ifdef CONFIG_KBUS_MAX_NUM_DEVICES
#define KBUS_MAX_NUM_DEVICES		CONFIG_KBUS_MAX_NUM_DEVICES
#else
#define KBUS_MAX_NUM_DEVICES		256
#endif

#ifndef CONFIG_KBUS_DEF_NUM_DEVICES
#define CONFIG_KBUS_DEF_NUM_DEVICES	1
#endif

/*
 * Our initial array sizes could arguably be made configurable
 * for tuning, if we discover this is useful
 */
#define KBUS_INIT_MSG_ID_MEMSIZE	16
#define KBUS_INIT_LISTENER_ARRAY_SIZE	8

/*
 * Setting CONFIG_KBUS_DEBUG will cause the Makefile
 * to define DEBUG for us
 */
#ifdef DEBUG
#define kbus_maybe_dbg(kbus_dev, format, args...) do { \
	if ((kbus_dev)->verbose) \
		(void) dev_dbg((kbus_dev)->dev, format, ## args); \
} while (0)
#else
#define kbus_maybe_dbg(kbus_dev, format, args...) ((void)0)
#endif

/*
 * This is really only directly useful if CONFIG_KBUS_DEBUG is on
 */
#ifdef CONFIG_KBUS_DEBUG_DEFAULT_VERBOSE
#define KBUS_DEFAULT_VERBOSE_SETTING true
#else
#define KBUS_DEFAULT_VERBOSE_SETTING false
#endif

/* ========================================================================= */

/* We need a way of remembering message bindings */
struct kbus_message_binding {
	struct list_head list;
	struct kbus_private_data *bound_to;	/* who we're bound to */
	u32 bound_to_id;	/* but the id is often useful */
	u32 is_replier;		/* bound as a replier */
	u32 name_len;
	char *name;		/* the message name */
};

/*
 * For both keeping track of requests sent (to which we still want replies)
 * and replies read (to which we haven't yet sent a reply), we need some
 * means of remembering message ids. Since I'd rather not worry the rest of
 * the code with how this is implemented (which is code for "I'll implement
 * it very simply and worry about making it efficient/scalable later"), and
 * since we always want to remember both the message ids and also how many
 * there are, it seems sensible to bundle this up in its own datastructure.
 */
struct kbus_msg_id_mem {
	u32 count;	/* Number of entries in use */
	u32 size;	/* Actual size of the array */
	u32 max_count;	/* Max 'count' we've had */
	/*
	 * An array is probably the worst way to store a list of message ids,
	 * but it's *very simple*, and should work OK for a smallish number of
	 * message ids. So it's a place to start...
	 *
	 * Note the array may have "unused" slots, signified by message id {0:0}
	 */
	struct kbus_msg_id *ids;
};

/* An item in the list of requests that a Ksock has not yet replied to */
struct kbus_unreplied_item {
	struct list_head list;
	struct kbus_msg_id id;	/* the request's id */
	u32 from;		/* the sender's id */
	struct kbus_name_ptr *name_ref;	/* and its name... */
	u32 name_len;
};

/*
 * The parts of a message being written to KBUS (via kbus_write[_parts]),
 * or read by the user (via kbus_read) are:
 *
 * * the user-space message header - as in 'struct kbus_message_header'
 *
 * from which we copy various items into our own internal message header.
 *
 * For a "pointy" message, that is all there is.
 *
 * For an "entire" message, this is then followed by:
 *
 * * the message name
 * * padding to bring that up to a NULL terminator and then a 4-byte boundary.
 *
 * If the "entire" message has data, then this is followed by:
 *
 * * N data parts (all but the last of size PART_LEN)
 * * padding to bring that up to a 4-byte boundary.
 *
 * and finally, whether there was data or not:
 *
 * * the final end guard.
 *
 * Remember that kbus_read always delivers an "entire" message.
 */
enum kbus_msg_parts {
	KBUS_PART_HDR = 0,
	KBUS_PART_NAME,
	KBUS_PART_NPAD,
	KBUS_PART_DATA,
	KBUS_PART_DPAD,
	KBUS_PART_FINAL_GUARD
};
/* N.B. New message parts require switch cases in kbus_msg_part_name()
 * and kbus_write_parts().
 */

#define KBUS_NUM_PARTS (KBUS_PART_FINAL_GUARD+1)

/*
 * Replier typing.
 * The higher the replier type, the more specific it is.
 * We trust the binding mechanisms not to have created two replier
 * bindings of the same type for the same name (so we shan't, for
 * example, get '$.Fred.*' bound as replier twice).
 */

enum kbus_replier_type {
	UNSET = 0,
	WILD_STAR,
	WILD_PERCENT,
	SPECIFIC
};

/*
 * A reference counting wrapper for message data
 *
 * If 'as_pages' is false, then the data is stored as a single kmalloc'd
 * entity, pointed to by 'parts[0]'. In this case, 'num_parts' will be 1,
 * and 'last_page_len' will be the size of the allocated data.
 *
 * If 'as_pages' is true, then the data is stored as 'num_parts' pages, each
 * pointed to by 'parts[n]'. The last page should be treated as being size
 * 'last_page_len' (even if the implementation is not enforcing this). All
 * other pages are of size PART_LEN.
 *
 * In either case, 'lengths[n]' is a "fill counter" for how many bytes of data
 * are actually being stored in page 'n'. Once the data is all in place, this
 * should be equal to PART_LEN or 'last_page_len' as appropriate.
 *
 * 'refcount' is a stanard kernel reference count for the data - when it reaches
 * 0, everything (the parts, the arrays and the datastructure) gets freed.
 */
struct kbus_data_ptr {
	int as_pages;
	unsigned num_parts;
	unsigned long *parts;
	unsigned *lengths;
	unsigned last_page_len;
	struct kref refcount;
};

/*
 * A reference counting wrapper for message names
 *
 * RATIONALE:
 *
 * When a message name is copied from user space, we take our first copy.
 *
 * In order to send a message to a Ksock, we use kbus_push_message(), which
 * takes a copy of the message for each recipient.
 *
 * We also take a copy of the message name for our list of messages that have
 * been read but not replied to.
 *
 * It makes sense to copy the message header, because the contents thereof are
 * changed according to the particular recipient.
 *
 * Copying the message data (if any) is handled by the kbus_data_ptr, above,
 * which provides reference counting. This makes sense because message data may
 * be large.
 *
 * If we only have one recipient, copying the message name is not a big issue,
 * but if there are many, we would prefer not to make many copies of the
 * string. It is, perhaps, worth keeping a dictionary of message names. and
 * referring to the name in that - but that's not an incremental change from
 * the "simple copying" state we start from.
 *
 * The simplest change to make, which may have some benefit, is to reference
 * count the names for an individual message, as is done for the message data.
 *
 * If we have a single recipient, we will have copied the string from user
 * space, and also created the kbus_name_ptr datastructure - an overhead of 8
 * bytes. However, when we copy the message for the recipient, we do not need
 * to copy the message name, so if the message name is more than 8 bytes, we
 * have immediately made a gain (and experience shows that message names tend
 * to be at least that long).
 *
 * As soon as we have more than one recipient, it becomes extremely likely that
 * we have saved space, and we will definitely have saved allocations which
 * could be fragmenting memory. So it sounds like a good thing to try.
 *
 * Also, if I later on want to store a hash code for the string (hoping to
 * speed up comparisons), the new datastructure gives me somewhere to put it...
 */
struct kbus_name_ptr {
	char *name;
	struct kref refcount;
};

/*
 * When the user reads a message from us, they receive a kbus_entire_message
 * structure.
 *
 * When the user writes a message to us, they write a "pointy" message, using
 * the kbus_message_header structure, or an "entire" message, using the
 * kbus_entire_message structure.
 *
 * Within the kernel, all messages are held as "pointy" messages, but instead
 * of direct pointers to the message name and data, we use reference counted
 * pointers.
 *
 * Rather than overload the 'name' and 'data' pointer fields, with all the
 * danger of getting it wrong that that implies, it seems simpler to have our
 * own, internal to the kernel, clone of the datastructure, but with these
 * fields defined correctly...
 *
 * Hmm. If we have the name and data references, perhaps we should move the
 * name and data *lengths* into those same.
 */

struct kbus_msg {
	struct kbus_msg_id id;	/* Unique to this message */
	struct kbus_msg_id in_reply_to;	/* Which message this is a reply to */
	u32 to;		/* 0 (empty) or a replier id */
	u32 from;	/* 0 (KBUS) or the sender's id */
	struct kbus_orig_from orig_from;	/* Cross-network linkage */
	struct kbus_orig_from final_to;	/* Cross-network linkage */
	u32 extra;	/* ignored field - future proofing */
	u32 flags;	/* Message type/flags */
	u32 name_len;	/* Message name's length, in bytes */
	u32 data_len;	/* Message length, also in bytes */
	struct kbus_name_ptr *name_ref;
	struct kbus_data_ptr *data_ref;
};

/*
 * The current message that the user is reading (with kbus_read())
 *
 * If 'msg' is NULL, then the data structure is "empty" (i.e., there is no
 * message being read).
 */
struct kbus_read_msg {
	struct kbus_entire_message user_hdr;	/* the header for user space */

	struct kbus_msg *msg;	/* the internal message */
	char *parts[KBUS_NUM_PARTS];
	unsigned lengths[KBUS_NUM_PARTS];
	int which;		/* The current item */
	u32 pos;		/* How far they've read in it */
	/*
	 * If the current item is KBUS_PART_DATA then we 'ref_data_index' is
	 * which part of the data we're in, and 'pos' is how far we are through
	 * that particular item.
	 */
	u32 ref_data_index;
};

/*
 * See kbus_write_parts() for how this data structure is actually used.
 *
 * If 'msg' is NULL, then the data structure is "empty" (i.e., there is no
 * message being written).
 *
 * * 'is_finished' is true when we've got all the bytes for our message,
 *   and thus don't want any more. It's an error for the user to try to
 *   write more message after it is finished.
 *
 *   For a "pointy" message, this is set immediately after the message header
 *   end guard is finished (the message name and any data aren't "pulled in"
 *   until the user does SEND). For an "entire" message, this is set after the
 *   final end guard is finished (so we will have the message name and any data
 *   in memory).
 *
 * * 'pointers_are_local' is true if the message's name and data have been
 *   transferred to kernel space (as reference counted entities), and false
 *   if they are (still) in user space.
 *
 * * 'hdr' is the message header, the shorter in-kernel version.
 *
 * * 'which' indicates which part of the message we think we're being given
 *   bytes for, from KBUS_PART_HDR through to (for an "entire" message)
 *   KBUS_PART_FINAL_GUARD.
 * * 'pos' is the index of the next byte within the current part of whatever
 *   we're working on, as indicated by 'which'. Note that for message data,
 *   this is the index within the whole of the data (not the index within a
 *   data part).
 *
 * * If we're reading an "entire" message, then the message name gets written
 *   to 'ref_name', which is a reference-counted string. This is allocated to
 *   the correct size/shape for the entire message name, after the head has
 *   been read.
 *
 *   The intention is that, if 'ref_name' is non-NULL, it should be legal
 *   to call 'kbus_lower_name_ref()' on it, to free its contents.
 *
 * * Similarly, 'ref_data' is reference-counted data, again allocated to the
 *   correct size/shape for the entire message data length, after the header
 *   has been read. The 'length' for each part is used to indicate how far
 *   through that part we have populated with bytes.
 *
 *   The intention is that, if 'ref_data' is non-NULL, it should be legal
 *   to call 'kbus_lower_data_ref()' on it, to free its contents.
 *
 *  'ref_data_index' is then the index (starting at 0) of the referenced
 *   data part that we are populating.
 */
struct kbus_write_msg {
	struct kbus_entire_message user_msg;	/* from user space */
	struct kbus_msg *msg;	/* our version of it */

	u32 is_finished;
	u32 pointers_are_local;
	u32 guard;		/* Whichever guard we're reading */
	char *user_name_ptr;	/* User space name */
	void *user_data_ptr;	/* User space data */
	enum kbus_msg_parts which;
	u32 pos;
	struct kbus_name_ptr *ref_name;
	struct kbus_data_ptr *ref_data;
	u32 ref_data_index;
};

/*
 * This is the data for an individual Ksock
 *
 * Each time we open /dev/kbus<n>, we need to remember a unique id for
 * our file-instance. Using 'filp' might work, but it's not something
 * we have control over, and in particular, if the file is closed and
 * then reopened, there's no guarantee that a particular value of 'filp'
 * won't be used again. A simple serial number is safer.
 *
 * Each such "opening" also has a message queue associated with it. Any
 * messages this "opening" has declared itself a listener (or replier)
 * for will be added to that queue.
 *
 * 'id' is the unique id for this file descriptor - it enables stateful message
 * transtions, etc. It is local to the particular KBUS device.
 *
 * 'last_msg_id_sent' is the message id of the last message that was
 * (successfully) written to this file descriptor. It is needed when
 * constructing a reply.
 *
 * We have a queue of messages waiting for us to read them, in 'message_queue'.
 * 'message_count' is how many messages are in the queue, and 'max_messages'
 * is an indication of how many messages we shall allow in the queue.
 *
 * Note that, however a message was originally sent to us, messages held
 * internally are always a message header plus pointers to a message name and
 * (optionally) message data. See kbus_send() for details.
 */
struct kbus_private_data {
	struct list_head list;
	struct kbus_dev *dev;	/* Which device we are on */
	u32 id;		/* Our own id */
	struct kbus_msg_id last_msg_id_sent;	/* As it says - see above */
	u32 message_count;	/* How many messages for us */
	u32 max_messages;	/* How many messages allowed */
	struct list_head message_queue;	/* Messages for us */

	/*
	 * It's useful (for /proc/kbus/bindings) to remember the PID of the
	 * current process
	 */
	pid_t pid;

	/* Wait for something to appear in the message_queue */
	wait_queue_head_t read_wait;

	/* The message currently being read by the user */
	struct kbus_read_msg read;

	/* The message currently being written by the user */
	struct kbus_write_msg write;

	/* Are we currently sending that message? */
	int sending;

	/*
	 * Each request we send should (eventually) generate us a reply, or
	 * at worst a status message from KBUS itself telling us there isn't
	 * going to be one. So we need to ensure that there is room in our
	 * (as the sender) message queue to receive all/any such.
	 *
	 * Note that this *also* allows SEND to forbid sending a Reply to a
	 * Request that we did not receive (or to which we have already
	 * replied)
	 */
	struct kbus_msg_id_mem outstanding_requests;

	/*
	 * If we are a replier for a message, then KBUS wants to ensure
	 * that a reply is *definitely* made. If we release ourselves, then
	 * we're clearly not going to reply to any requests that we have
	 * read but not replied to, and KBUS would like to generate a status
	 * message for each such. So we need a list of the information needed
	 * to form such Status/Reply messages.
	 *
	 *     (Thus we don't need the whole of the original message, since
	 *     we're only *really* needing its name, its id and who its
	 *     from -- given which its easiest just to keep the parts we
	 *     *do* need, and ignore the data.)
	 *
	 * It was decided not to place a limit on the size of this list.
	 * Its size is limited by the ability of sender(s) to send
	 * requests, which in turn is limited by the the number of slots
	 * they can reserve for the replies to those requests in their
	 * own message queues.
	 *
	 * If a limit was imposed, then we would also need to stop a sender
	 * sending a request because the replier has too many replies
	 * outstanding (for instance, because it has gone to sleep). But
	 * then we'd assume that it is not responding to messages in
	 * general, and so its message queue would fill up, and that
	 * should be sufficient protection.
	 */
	struct list_head replies_unsent;
	u32 num_replies_unsent;
	u32 max_replies_unsent;

	/*
	 * Managing which messages a replier may reply to
	 * ----------------------------------------------
	 * We need to police replying, such that a replier may only reply
	 * to requests that it has received (where "received" means "had
	 * placed into its message queue", because KBUS must reply for us
	 * if the particular Ksock is not going to).
	 *
	 * It is possible to do this using either the 'outstanding_requests'
	 * or the 'replies_unsent' list.
	 *
	 * Using the 'outstanding_requests' list means that when a replier
	 * wants to send a reply, it needs to look up who the original-sender
	 * is (from its Ksock id, in the "from" field of the message), and
	 * check against that. This is a bit inefficient.
	 *
	 * Using the 'replies_unsent' list means that when a replier wants
	 * to send a reply, it just needs to find the right message stub
	 * in said 'replies_unsent' list, and check that the reply *does*
	 * match the original request. This may be more efficient, depending.
	 *
	 * In fact, the 'outstanding_requests' list is used, simply because
	 * it was implemented first.
	 */

	/*
	 * By default, if a Ksock binds to a message name as both Replier and
	 * Listener (typically by binding to a specific message name as Replier
	 * and to a wildcard including it as Listener), and a Reqest of that
	 * name is sent to that Ksock, it will get the message once as Replier
	 * (marked "WANT_YOU_TO_REPLY"), and once as listener.
	 *
	 * This is technically sensible, but can be irritating to the end user
	 * who really often only wants to receive the message once.
	 *
	 * If "messages_only_once" is set, then when a message is about to be
	 * put onto a Ksocks message queue, it will only be added if it (i.e.,
	 * a message with the same id) has not already just been added. This
	 * is safe because Requests to the specific Replier are always dealt
	 * with first.
	 *
	 * As a side-effect, which I think also makes sense, this will also
	 * mean that if a Listener has bound to the same message name multiple
	 * times (as a Listener), then they will only get the message once.
	 */
	int messages_only_once;
	/*
	 * Messages can be added to either end of our message queue (i.e.,
	 * depending on whether they're urgent or not). This means that the
	 * "only once" mechanism needs to check both ends of the queue (which
	 * is a pain). Or we can just remember the message id of the last
	 * message pushed onto the queue. Which is much simpler.
	 */
	struct kbus_msg_id msg_id_just_pushed;
};

/* What is a sensible number for the default maximum number of messages? */
#ifndef CONFIG_KBUS_DEF_MAX_MESSAGES
#define CONFIG_KBUS_DEF_MAX_MESSAGES	100
#endif

/* Information belonging to each /dev/kbus<N> device */
struct kbus_dev {
	struct cdev cdev;	/* Character device data */
	struct device *dev;	/* Our very selves */

	u32 index;		/* Which /dev/kbus<n> device we are */

	/*
	 * The Big Lock
	 * We use a single mutex for all purposes, and all locking is done
	 * at the "top level", i.e., in the externally called functions.
	 * This simplifies the design of the internal (list processing,
	 * etc.) functions, at the possible cost of making interaction
	 * with KBUS, in general, slower.
	 *
	 * On the other hand, we favour reliable over fast.
	 */
	struct mutex mux;

	/* Who has bound to receive which messages in what manner */
	struct list_head bound_message_list;

	/*
	 * The actual Ksock entries (one per 'open("/dev/kbus<n>")')
	 * This is to allow us to find the 'kbus_private_data' instances,
	 * so that we can get at all the message queues. The details of
	 * how we do this are *definitely* going to change...
	 */
	struct list_head open_ksock_list;

	/* Has one of our Ksocks made space available in its message queue? */
	wait_queue_head_t write_wait;

	/*
	 * Each open file descriptor needs an internal id - this is used
	 * when binding messages to listeners, but is also needed when we
	 * want to reply. We reserve the id 0 as a special value ("none").
	 */
	u32 next_ksock_id;

	/*
	 * Every message sent has a unique id (again, unique per device).
	 */
	u32 next_msg_serial_num;

	/* Are we wanting debugging messages? */
	u32 verbose;

	/*
	 * Are we wanting to send a synthetic message for each Replier
	 * bind/unbind? */
	u32 report_replier_binds;
};

/*
 * Each entry in a message queue holds a single message, and a pointer to
 * the message name binding that caused it to be added to the list. This
 * makes it simple to remove messages from the queue if the message name
 * binding is unbound. The binding shall be NULL for:
 *
 *  * Replies
 *  * KBUS "synthetic" messages, which are also (essentialy) Replies
 */
struct kbus_message_queue_item {
	struct list_head list;
	struct kbus_msg *msg;
	struct kbus_message_binding *binding;
};

/* The sizes of the parts in our reference counted data */
#define KBUS_PART_LEN		PAGE_SIZE
#define KBUS_PAGE_THRESHOLD	(PAGE_SIZE >> 1)

/* Manage the files used to report KBUS internal state */
/* From kbus_internal.c */
#ifndef CONFIG_PROC_FS
void kbus_setup_reporting(void) {}
void kbus_remove_reporting(void) {}
#else
extern void kbus_setup_reporting(void);
extern void kbus_remove_reporting(void);
#endif
/* From kbus.c itself */
extern void kbus_get_device_data(int *num_devices,
				 struct kbus_dev ***devices);
extern u32 kbus_lenleft(struct kbus_private_data *priv);

#endif /* _kbus_internal */
