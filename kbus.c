/* Kbus kernel module
 *
 * This is a character device driver, providing the messaging support
 * for kbus.
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

#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/device.h>       /* device classes (for hotplugging), &c */
#include <linux/cdev.h>         /* registering character devices */
#include <linux/list.h>
#include <linux/bitops.h>	/* for BIT */
#include <linux/ctype.h>	/* for isalnum */
#include <asm/uaccess.h>	/* copy_*_user() functions */

/*
 * The actual number of /dev/kbus<N> devices can be set at module
 * startup. We set hard limits so it is between 1 and 10, and default
 * to 1.
 *
 * For instance::
 *
 *     # insmod kbus.ko kbus_num_devices=5
 */
#define MIN_NUM_DEVICES		 1
#define MAX_NUM_DEVICES		10
#define DEF_NUM_DEVICES		 1

static int  kbus_num_devices = DEF_NUM_DEVICES;

/* Who we are -- devices */
static int  kbus_major = 0;             /* We'll go for dynamic allocation */
static int  kbus_minor = 0;             /* We're happy to start with device 0 */

static struct class      *kbus_class_p;
static struct device    **kbus_class_devices;

/* When the user asks to bind a message name to an interface, they use: */
struct kbus_m_bind_struct {
	uint32_t	 replier;	/* are we a replier? */
	uint32_t	 guaranteed;	/* do we receive *all* the messages? */
	uint32_t	 len;		/* length of name */
	char		*name;
};

/* When the user requests the id of the replier to a message, they use: */
struct kbus_m_bind_query_struct {
	uint32_t	 return_id;
	uint32_t	 len;
	char		*name;
};

/* When the user writes/reads a message, they use: */
struct kbus_message_struct {
	/*
	 * The guards
	 * ----------
	 *
	 * * 'start_guard' is notionally "kbus", and 'end_guard' (the 32 bit
	 *   word after the rest of the message) is notionally "subk". Obviously
	 *   that depends on how one looks at the 32-bit word. Every message
	 *   shall start with a start guard and end with an end guard.
	 *
	 * These provide some help in checking that a message is well formed,
	 * and in particular the end guard helps to check for broken length
	 * fields.
	 *
	 * The message header
	 * ------------------
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
	 *   When replying to a message, it should be set to the 'from' value
	 *   of the orginal message
	 *
	 *   In a "stateful dialogue", 'to' can be set to the id of the
	 *   expected replier, in which case KBUS will raise an
	 *   exception/return an appropriate synthetic message if that replier
	 *   is no longer the bound replier for this message,
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
	 * The message body
	 * ----------------
	 *
	 * - 'name_len' is the length of the message name in bytes.
	 *
	 *   When writing a new message, this must be non-zero.
	 *
	 *   When replying to a message, this must be non-zero - i.e., the
	 *   message name must still be given - although it is possible
	 *   that this may change in the future.
	 *
	 * - 'data_len' is the length of the message data, in 32-bit
	 *   words. It may be zero.
	 *
	 * - 'name_and_data' is the bytes of message name immediately
	 *   followed by the bytes of message data.
	 *
	 *   * The message name must be padded out to a multiple of 4 bytes.
	 *     This is not indicated by the message length. Padding should be
	 *     with zero bytes (but it is not necessary for there to be a zero
	 *     byte at the end of the name). Byte ordering is according to that
	 *     of the platform, treating msg->name_and_data[0] as the start of
	 *     an array of bytes.
	 *
	 *   * The data is not touched by KBUS, and may include any values.
	 *
	 * The last element of 'name_and_data' must always be an end guard,
	 * with value 0x7375626B ('subk') (so the *actual* data is always one
	 * 32-bit word shorter than that indicated).
	 */
	uint32_t	 start_guard;
	uint32_t	 id;		/* Unique to this message */
	uint32_t	 in_reply_to;	/* Which message this is a reply to */
	uint32_t	 to;		/* 0 (normally) or a replier id */
	uint32_t	 from;		/* 0 (KBUS) or the sender's id */
	uint32_t	 flags;		/* Message type/flags */
	uint32_t	 name_len;	/* Message name's length, in bytes */
	uint32_t	 data_len;	/* Message length, as 32 bit words */
	uint32_t	 rest[];	/* Message name, data and end_guard */
};
/* XXX and, at least for the moment, we also use this internally */

#define KBUS_MSG_START_GUARD	0x7375626B
#define KBUS_MSG_END_GUARD	0x6B627573

/*
 * Flags for the message 'flags' word
 *
 * The KBUS_BIT_WANT_A_REPLY bit is set by the sender to indicate that a
 * reply is wanted.
 *
 * The KBUS_BIT_WANT_YOU_TO_REPLY is set by KBUS on a particular message
 * to indicate that the particular recipient is responsible for replying
 * to (this instance of the) message.
 */
#define	KBUS_BIT_WANT_A_REPLY		BIT(0)
#define KBUS_BIT_WANT_YOU_TO_REPLY	BIT(1)

/*
 * Given name_len (in bytes) and data_len (in 32-bit words), return the
 * length of the appropriate kbus_message_struct, in bytes
 *
 * Don't forget the end guard at the end...
 *
 * Remember that "sizeof" doesn't count the 'rest' field in our message
 * structure.
 *
 * (it's sensible to use "sizeof" so that we don't need to amend the macro
 * if the message datastructure changes)
 */
#define KBUS_MSG_LEN(name_len,data_len)    (sizeof(struct kbus_message_struct) + \
					    4 * ((name_len+3)/4 + data_len + 1))

/*
 * The message name starts at msg->rest[0].
 * The message data starts after the message name - given the message
 * name's length (in bytes), that is at index:
 */
#define KBUS_MSG_DATA_INDEX(name_len)     ((name_len+3)/4)
/*
 * Given the message name length (in bytes) and the message data length (in
 * 32-bit words), the index of the end guard is thus:
 */
#define KBUS_MSG_END_GUARD_INDEX(name_len,data_len)  ((name_len+3)/4 + data_len)


/* We need a way of remembering message bindings */
struct kbus_message_binding {
	struct list_head list;
	uint32_t	 bound_to;	/* who we're bound to */
	uint32_t	 replier;	/* true if bound as a replier */
	uint32_t	 guaranteed;	/* true if we insist on all messages */
	uint32_t	 len;		/* useful, I think */
	char		*name;		/* "$.a.b.c" */
};

/* XXX REVIEW THESE VALUES XXX */
#define KBUS_MAX_NAME_LEN	1000	/* Maximum message name length (bytes) */
#define KBUS_MAX_DATA_LEN	1000	/* Maximum message data length (32-bit words) */

/*
 * Each time we open /dev/kbus0, we need to remember a unique id for
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
 * 'last_msg_id' is the message id of the last message that was (successfully)
 * written to this file descriptor. It is needed when constructing a reply.
 *
 * XXX 'message_count' is a count of the number of messages currently in the
 * XXX 'message_queue' -- i.e., the number of messages waiting to be read. I'm
 * XXX not * convinced that this is actually necessary, and it might go away
 * XXX later on.
 */
struct kbus_private_data {
	struct list_head	 list;
	struct kbus_dev		*dev;		/* Which device we are on */
	uint32_t	 	 id;		/* Our own id */
	uint32_t		 last_msg_id;	/* Last message written to us */
	uint32_t		 message_count;	/* How many messages for us */
	struct list_head	 message_queue;	/* Messages for us */

	/* The message currently being read by the user */
	char		*read_msg;
	size_t		 read_msg_len;		/* Its length */
	size_t		 read_msg_pos;		/* How far they've read */
};

/* Information belonging to each /dev/kbus<N> device */
struct kbus_dev
{
	struct cdev   		cdev;	/* Character device data */

	/*
	 * The Big Lock
	 * For the moment, try having a single semaphore for all purposes
	 * - we can do more specific locking later on if it proves useful.
	 *
	 * For the moment, all locking is done at the "top level", i.e.,
	 * in the externally called functions. This simplifies the design
	 * of the internal (list processing, etc.) functions, at the possible
	 * cost of making interaction with kbus, in general, slower.
	 * 
	 * On the other hand, we are not intending to provide a *fast* system.
	 */
	struct semaphore	sem;

	/* Who has bound to receive which messages in what manner */
	struct list_head	bound_message_list;

	/*
	 * The actual listeners (one per 'open("/dev/kbus0")') 
	 * (XXX can I think of a better name for this? XXX)
	 * This is to allow us to find the 'kbus_private_data' instances,
	 * so that we can get at all the message queues. The details of
	 * how we do this are *definitely* going to change...
	 */
	struct list_head	open_files_list;

	/*
	 * Each open file descriptor needs an internal id - this is used
	 * when binding messages to listeners, but is also needed when we
	 * want to reply. We reserve the id 0 as a special value ("none").
	 */
	uint32_t		next_file_id;

	/*
	 * Every message sent has a unique id (again, unique per device).
	 * We're the obvious keeper of this...
	 */
	uint32_t		next_msg_id;
};

/* Our actual devices, 0 through kbus_num_devices */
static struct kbus_dev       *kbus_devices;

/*
 * Each entry in a message queue holds a single message.
 */
struct kbus_message_queue_item {
	struct list_head		 list;
	struct kbus_message_struct	*msg;
};

/*
 * Given a message name, is it valid?
 *
 * We have nothing to say on maximum length.
 *
 * Returns 0 if it's OK, 1 if it's naughty
 */
static int kbus_bad_message_name(char *name, size_t len)
{
	size_t	 ii;
	int	 dot_at = 1;

	if (len < 3) return 1;

	if (name == NULL || name[0] != '$' || name[1] != '.')
		return 1;

	if (name[len-2] == '.' && name[len-1] == '*')
		len -= 2;
	else if (name[len-2] == '.' && name[len-1] == '%')
		len -= 2;
	
	if (name[len-1] == '.') return 1;

	for (ii=2; ii<len; ii++) {
		if (name[ii] == '.')
		{
			if (dot_at == ii-1)
				return 1;
			dot_at = ii;
		}
		else if (!isalnum(name[ii]))
			return 1;
	}
	return 0;
}

/*
 * Does this message name match the given binding?
 *
 * The binding may be a normal message name, or a wildcard.
 *
 * We assume that both names are legitimate.
 */
static int kbus_message_name_matches(char *name, size_t name_len, char *other)
{
	size_t	 other_len = strlen(other);

	if (other[other_len-1] == '*' || other[other_len-1] == '%') {
		char	*rest = name + other_len - 1;
		size_t	 rest_len = name_len - other_len + 1;

		/*
		 * If we have '$.Fred.*', then we need at least '$.Fred.X' to match
		 */
		if (name_len < other_len)
			return false;
		/*
		 * Does the name match all of the wildcard except the last character?
		 */
		if (strncmp(other,name,other_len-1))
			return false;

		/* '*' matches anything at all, so we're done */
		if (other[other_len-1] == '*')
			return true;

		/* '%' only matches if we don't have another dot */
		if (strnchr(rest,rest_len,'.'))
			return false;
		else
			return true;
	} else {
		if (name_len != other_len)
			return false;
		else
			return (!strncmp(name,other,name_len));
	}
}

/*
 * Extract useful information from a message.
 *
 * Return 0 if the message is well strutured, -EINVAL if it is not.
 */
static int kbus_dissect_message(struct kbus_message_struct	 *msg,
				uint32_t			 *msg_len,
				char				**name_p,
				uint32_t			**data_p)
{
	int   		 data_idx, end_guard_idx;

	/*
	 * Check the guards match, and that lengths are "plausible"
	 */
	if (msg->start_guard != KBUS_MSG_START_GUARD) {
		printk(KERN_DEBUG "kbus: message start guard is %08x,"
		       " not %08x\n", msg->start_guard,
		       KBUS_MSG_START_GUARD);
		return -EINVAL;
	}

	if (msg->name_len > KBUS_MAX_NAME_LEN) {
		printk(KERN_DEBUG "kbus: message name length is %u,"
		       " more than %u\n", msg->name_len,
		       KBUS_MAX_NAME_LEN);
		return -ENAMETOOLONG;
	}
	if (msg->data_len > KBUS_MAX_DATA_LEN) {
		printk(KERN_DEBUG "kbus: message data length is %u,"
		       " more than %u\n", msg->data_len,
		       KBUS_MAX_DATA_LEN);
		return -EMSGSIZE;
	}

	*msg_len = KBUS_MSG_LEN(msg->name_len,msg->data_len);

	data_idx = KBUS_MSG_DATA_INDEX(msg->name_len);
	end_guard_idx = KBUS_MSG_END_GUARD_INDEX(msg->name_len,
						 msg->data_len);
	if (msg->rest[end_guard_idx] != KBUS_MSG_END_GUARD) {
		printk(KERN_DEBUG "kbus: message end guard is %08x,"
		       " not %08x\n", msg->rest[end_guard_idx],
		       KBUS_MSG_END_GUARD);
		return -EINVAL;
	}
	*name_p = (char *)&msg->rest[0];
	*data_p = &msg->rest[data_idx];

	if (kbus_bad_message_name(*name_p,msg->name_len)) {
		printk(KERN_DEBUG "kbus: message name '%*s' is not allowed\n",
		       msg->name_len,*name_p);
		return -EBADMSG;
	}
	return 0;
}

/*
 * Return 0 if a message is well-formed, negative otherwise.
 */
static int kbus_check_message(struct kbus_message_struct	*msg)
{
	/* This is the simplest way to do this... */
	uint32_t	 msg_len;
	char		*name_p;
	uint32_t	*data_p;
	return kbus_dissect_message(msg,&msg_len,&name_p,&data_p);
}

/*
 * Output a description of the message
 */
static void kbus_report_message(char				*kern_prefix,
				struct kbus_message_struct	*msg)
{
	uint32_t	 msg_len;
	char		*name_p;
	uint32_t	*data_p;
	if (!kbus_dissect_message(msg,&msg_len,&name_p,&data_p))
		printk("%skbus: message id:%u to:%u from:%u"
		       " flags:%08x name:'%*s' data/%u:%08x\n",
		       kern_prefix,
		       msg->id,msg->to,msg->from,msg->flags,msg->name_len,
		       name_p,msg->data_len,data_p[0]);
}

/*
 * Copy the given message from user space, and add it to the end of the queue
 *
 * We assume the message has been checked for sanity.
 *
 * 'for_replier' is true if this particular message is being pushed to the
 * message's replier's queue.
 *
 * Note that 'msg_len' is the number of *bytes* there are in the message.
 *
 * Returns 0 if all goes well, or a negative error.
 */
static int kbus_push_message(struct kbus_private_data	  *priv,
			     uint32_t			   msg_len,
			     struct kbus_message_struct   *msg,
			     int			   for_replier)
{
	struct list_head		*queue = &priv->message_queue;
	struct kbus_message_struct	*new_msg;
	struct kbus_message_queue_item	*item;

	printk(KERN_DEBUG "kbus: ** Pushing message onto queue (for %s)\n",
	       for_replier?"replier":"listener");

	/* Check it makes some degreee of sense */
	if (kbus_check_message(msg)) return -EINVAL;

	new_msg = kmalloc(msg_len, GFP_KERNEL);
	if (!new_msg) {
		printk(KERN_ERR "kbus: Cannot kmalloc copy of message\n");
	       	return -EFAULT;
	}

	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		printk(KERN_ERR "kbus: Cannot kmalloc new message item\n");
		kfree(new_msg);
	       	return -EFAULT;
	}

	if (copy_from_user(new_msg,msg,msg_len)) {
		printk(KERN_ERR "kbus: Cannot copy message\n");
		kfree(new_msg);
		kfree(item);
		return -EFAULT;
	}

	/* XXX Just in case */
	printk(KERN_DEBUG "kbus: Message copied to add to queue\n");
	kbus_report_message(KERN_DEBUG,new_msg);

	if (for_replier && (KBUS_BIT_WANT_A_REPLY & msg->flags)) {
		/*
		 * This message wants a reply, and is for the message's
		 * replier, so they need to be told that they are to reply to
		 * this message
		 */
		new_msg->flags |= KBUS_BIT_WANT_YOU_TO_REPLY;
		printk(KERN_DEBUG "kbus: Setting WANT_YOU_TO_REPLY flag %08x\n",
		       new_msg->flags);
	} else {
		/*
		 * The recipient is *not* the replier for this message,
		 * so it is not responsible for replying.
		 */
		new_msg->flags &= ~KBUS_BIT_WANT_YOU_TO_REPLY;
	}

	/* And join it up... */

	new_msg->from = priv->id;	/* Remember it is from us */
	item->msg = new_msg;

	/*
	 * We want a FIFO, so add to the end of the list (just before the list
	 * head)
	 */
	list_add_tail(&item->list, queue);

	priv->message_count ++;

	printk(KERN_DEBUG "kbus: Leaving %d messages in queue\n",
	       priv->message_count);

	return 0;
}

/*
 * Pop the next message off our queue.
 *
 * Returns a pointer to the message, or NULL if there is no next message.
 */
static struct kbus_message_struct *kbus_pop_message(struct kbus_private_data *priv)
{
	struct list_head		*queue = &priv->message_queue;
	struct kbus_message_queue_item	*item;
	struct kbus_message_struct	*msg = NULL;

	printk(KERN_DEBUG "kbus: ** Popping message from queue\n");

	if (list_empty(queue))
		return NULL;

	/* Retrieve the next message */
	item = list_first_entry(queue, struct kbus_message_queue_item, list);

	/* And straightway remove it from the list */
	list_del(&item->list);

	priv->message_count --;

	msg = item->msg;
	kfree(item);

	/* XXX Report just in case */
	printk(KERN_DEBUG "kbus: Message popped from queue\n");
	kbus_report_message(KERN_DEBUG, msg);


	printk(KERN_DEBUG "kbus: Leaving %d messages in queue\n",
	       priv->message_count);

	return msg;
}

/*
 * Return the length of the next message waiting to be read (if any) on this
 * listener's file descriptor
 *
 * Returns 0 if there is no message, the message size (in bytes) if there is,
 * and a negative number if something goes wrong.
 */
static int kbus_next_message_len(struct kbus_private_data  *priv)
{
	struct list_head		*queue = &priv->message_queue;
	struct kbus_message_queue_item	*ptr;
	struct kbus_message_struct	*msg;
	int    retval = 0;

	printk(KERN_DEBUG "kbus: ** Looking for length of next message\n");

	if (list_empty(queue))
		return 0;

	/* Retrieve the next message */
	ptr = list_first_entry(queue, struct kbus_message_queue_item, list);
	msg = ptr->msg;
	retval = KBUS_MSG_LEN(msg->name_len,msg->data_len);

	printk(KERN_DEBUG "kbus: First message len %d\n",retval);
	kbus_report_message(KERN_DEBUG,msg);

	return retval;
}

/*
 * Empty a message queue.
 */
static int kbus_empty_message_queue(struct kbus_private_data  *priv)
{
	struct list_head		*queue = &priv->message_queue;
	struct kbus_message_queue_item	*ptr;
	struct kbus_message_queue_item	*next;

	printk(KERN_DEBUG "kbus: ** Emptying message queue\n");

	list_for_each_entry_safe(ptr, next, queue, list) {

		/* XXX Let the user know */
		printk(KERN_DEBUG "kbus: Deleting message from queue\n");
		(void) kbus_check_message(ptr->msg);

		/* Remove it from the list */
		list_del(&ptr->list);
		/* And forget all about it... */
		kfree(ptr->msg);
		kfree(ptr);

		priv->message_count --;
	}
	printk(KERN_DEBUG "kbus: Leaving %d messages in queue\n",
	       priv->message_count);
	return 0;
}

/*
 * Find out who, if anyone, is bound as a replier to the given message name.
 *
 * Returns 1 if we found a replier, 0 if we did not (but all went well), and
 * a negative value if something went wrong.
 */
static int kbus_find_replier(struct kbus_dev	*dev,
			     uint32_t		*bound_to,
			     uint32_t		 len,
			     char		*name)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	/* We don't want anyone writing to the list whilst we do this */
	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		/*
		 * We are only interested in a replier binding to the name.
		 * We *could* check for the name and then check for reply-ness
		 * - if we found a name match that was *not* a replyer, then we'd
		 * have finished. However, checking the name is expensive, and I
		 * rather assume that a caller is only checking if they expect a
		 * positive result, so it's simpler to do a lazier check.
		 */
		if ( ptr->replier &&
		     ptr->len == len &&
		     !strncmp(name,ptr->name,len) ) {
			printk(KERN_DEBUG "kbus: %d:'%s' has replier %u\n",
			       ptr->len,ptr->name,
			       ptr->bound_to);
			*bound_to = ptr->bound_to;
			return 1;
		}
	}
	return 0;
}

/*
 * Find out who, if anyone, is bound as a listener to the given message name.
 *
 * 'listeners' is an array of (just) listener ids. It may be NULL (if there are
 * no listeners or if there was an error). It is up to the caller to free it.
 *
 * If one of the listeners was also a replier for this message, then 'replier'
 * will be its id, otherwise it will be zero. The replier will not be in the
 * 'listeners' array, so the caller must check both.
 *
 * Note that a particular listener id may be present more than once, if that
 * particular listener has bound to the message more than once.
 *
 * Returns the number of listeners found (i.e., the length of the array), and a
 * negative value if something went wrong. This is a bit clumsy, because the
 * caller needs to check the return value *and* the 'replier' value, but there
 * is only one caller, so...
 */
static int kbus_find_listeners(struct kbus_dev	 *dev,
			       uint32_t		**listeners,
			       uint32_t		 *replier,
			       uint32_t		  name_len,
			       char		 *name)
{
	/* XXX Silly values for debugging XXX */
	/*
	 * XXX Using low values here should provoke any problems,
	 * XXX but more realistic values should be used later.
	 */
#define INIT_SIZE	2
#define INCR_SIZE	2
	int count = 0;
	int array_size = INIT_SIZE;
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	/*
	 * The higher the replier type, the more specific it is.
	 * We trust the binding mechanisms not to have created two replier
	 * bindings of the same type for the same name (so we shan't get
	 * '$.Fred.*' bound as replier twice).
	 */
	enum replier_type {
		UNSET,
		WILD_STAR,
		WILD_PERCENT,
		SPECIFIC
	};
#define REPLIER_TYPE(r)		((r)==UNSET?"UNSET": \
				 (r)==WILD_STAR?"WILD_STAR": \
				 (r)==WILD_PERCENT?"WILD_PERCENT": \
				 (r)==SPECIFIC?"SPECIFIC":"???")
       	enum replier_type	replier_type = UNSET;
       	enum replier_type	new_replier_type = UNSET;

	printk(KERN_DEBUG "kbus: Looking for listeners/repliers for %d:'%s'\n",
	       name_len,name);

	*listeners = kmalloc(sizeof(uint32_t) * array_size,GFP_KERNEL);
	if (!(*listeners)) return -EFAULT;

	*replier = 0;

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {

		if (kbus_message_name_matches(name,name_len,ptr->name)) {
			printk(KERN_DEBUG "kbus: '%*s' matches '%s' for listener %u%s\n",
			       name_len, name, ptr->name,
			       ptr->bound_to, (ptr->replier?" (replier)":""));

			if (ptr->replier) {
				/* It *may* be the replier for this message */
				size_t	last_char = strlen(ptr->name) - 1;
				if (ptr->name[last_char] == '*')
					new_replier_type = WILD_STAR;
				else if (ptr->name[last_char] == '%')
					new_replier_type = WILD_PERCENT;
				else
					new_replier_type = SPECIFIC;

				printk(KERN_DEBUG "kbus: Replier was %u (%s), looking at %u (%s)\n",
				       *replier, REPLIER_TYPE(replier_type),
				       ptr->bound_to, REPLIER_TYPE(new_replier_type));
				/*
				 * If this is the first replier, just remember
				 * it. Otherwise, if it's more specific than
				 * our previous replier, remember it instead.
				 */
				if (*replier == 0 ||
				    new_replier_type > replier_type) {
					printk(KERN_DEBUG "kbus:  going with replier %u\n",
					       ptr->bound_to);
					*replier = ptr->bound_to;
					replier_type = new_replier_type;
				}
			} else {
				/* It is a listener */
				if (count == array_size) {
					printk(KERN_DEBUG "kbus: XXX listener array size %d -> %d\n",
					       array_size, array_size + INCR_SIZE);
					array_size += INCR_SIZE;
					*listeners = krealloc(*listeners,
							      sizeof(uint32_t) * array_size,
							      GFP_KERNEL);
					if (!(*listeners))
						return -EFAULT;
				}
				(*listeners)[count++] = ptr->bound_to;
			}
		}
	}
	printk(KERN_DEBUG "kbus: Found %d listener%s%s for '%s'\n",
	       count, (count==1?"":"s"),
	       (*replier==0?"":" and a replier"),
	       name);

	return count;
}

/*
 * Add a new binding.
 *
 * Doesn't allow more than one replier to be bound for a message name.
 *
 * NB: If it succeeds, then it wants to keep hold of 'name', so don't
 *     free it...
 *
 * Returns 0 if all went well, a negative value if it did not. Specifically,
 * -EADDRINUSE if an attempt was made to bind as a replier to a message name
 * that already has a replier bound.
 */
static int kbus_remember_binding(struct kbus_dev	*dev,
				 uint32_t		 bind_to,
				 uint32_t		 replier,
				 uint32_t		 guaranteed,
				 uint32_t		 len,
				 char			*name)
{
	int retval = 0;
	struct kbus_message_binding *new;

	/* If we want a replier, and there already is one, we lose */
	if (replier) {
		uint32_t  reply_to;
		retval = kbus_find_replier(dev, &reply_to, len, name);
		/*
		 * "Address in use" isn't quite right, but let's the caller
		 * have some hope of telling what went wrong, and this is a
		 * useful case to distinguish.
		 */
		if (retval == 1) return -EADDRINUSE;
	}

	/*
	 * Build our new datastructure, optimistically.
	 * This means that we can minimise the time we need to lock
	 * out list, at the cost of having to destroy our new structure
	 * if we *do* find a clash.
	 */
	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new) return -EFAULT;

	new->bound_to = bind_to;
	new->replier = replier;
	new->guaranteed = guaranteed;
	new->len = len;
	new->name = name;

	list_add(&new->list, &dev->bound_message_list);

	printk(KERN_DEBUG "kbus: Bound %u %c %c '%s'\n",
	       new->bound_to,
	       (new->replier?'R':'L'),
	       (new->guaranteed?'T':'F'),
	       new->name);
	return 0;
}

/*
 * Remove an existing binding.
 *
 * Returns 0 if all went well, a negative value if it did not.
 */
static int kbus_forget_binding(struct kbus_dev	*dev,
			       uint32_t		 bound_to,
			       uint32_t		 replier,
			       uint32_t		 guaranteed,
			       uint32_t		 len,
			       char		*name)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	/* We don't want anyone writing to the list whilst we do this */
	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		if (bound_to != ptr->bound_to)
			continue;
		if (replier != ptr->replier)
			continue;
		if (guaranteed != ptr->guaranteed)
			continue;
		if (len != ptr->len)
			continue;
		if ( !strncmp(name,ptr->name,len) ) {
			printk(KERN_DEBUG "kbus: Unbound %u %c %c '%s'\n",
			       ptr->bound_to,
			       (ptr->replier?'R':'L'),
			       (ptr->guaranteed?'T':'F'),
			       ptr->name);
			/* And we don't want anyone reading for this */
			list_del(&ptr->list);
			if (ptr->name)
				kfree(ptr->name);
			kfree(ptr);
			return 0;
		}
	}
	printk(KERN_DEBUG "kbus: Could not find/unbind %u %c %c '%s'\n",
	       bound_to,
	       (replier?'R':'L'),
	       (guaranteed?'T':'F'),
	       name);
	return -EINVAL;
}

/*
 * Remove all bindings for a particular listener.
 */
static void kbus_forget_my_bindings(struct kbus_dev	*dev,
				   uint32_t		 bound_to)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		if (bound_to == ptr->bound_to) {
			printk(KERN_DEBUG "kbus: Unbound %u %c %c '%s'\n",
			       ptr->bound_to,
			       (ptr->replier?'R':'L'),
			       (ptr->guaranteed?'T':'F'),
			       ptr->name);
			list_del(&ptr->list);
			if (ptr->name)
				kfree(ptr->name);
			kfree(ptr);
		}
	}
	return;
}

/*
 * Remove all bindings.
 *
 * Assumed to be called because the device is closing, and thus doesn't lock.
 */
static void kbus_forget_all_bindings(struct kbus_dev	*dev)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		printk(KERN_DEBUG "kbus: Unbinding %u %c %c '%s'\n",
		       ptr->bound_to,
		       (ptr->replier?'R':'L'),
		       (ptr->guaranteed?'T':'F'),
		       ptr->name);
		/* And we don't want anyone reading for this */
		list_del(&ptr->list);
		if (ptr->name)
			kfree(ptr->name);
		kfree(ptr);
	}
	return;
}

/*
 * Add a new open file to our remembrances.
 *
 * Returns 0 if all went well, a negative value if it did not.
 */
static int kbus_remember_open_file(struct kbus_dev		*dev,
				   struct kbus_private_data	*priv)
{
	list_add(&priv->list, &dev->open_files_list);

	printk(KERN_DEBUG "kbus: Remembered 'open file' id %u\n",priv->id);
	return 0;
}

/*
 * Retrieve the pointer to an open file's data
 *
 * Return NULL if we can't find it.
 */
static struct kbus_private_data *kbus_find_open_file(struct kbus_dev	*dev,
						     uint32_t		 id)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	list_for_each_entry_safe(ptr, next, &dev->open_files_list, list) {
		if (id == ptr->id) {
			printk(KERN_DEBUG "kbus: Found 'open file' id %u",id);
			return ptr;
		}
	}
	printk(KERN_DEBUG "kbus: Could not find 'open file' %u\n",id);
	return NULL;
}

/*
 * Remove an open file remembrance.
 *
 * Returns 0 if all went well, a negative value if it did not.
 */
static int kbus_forget_open_file(struct kbus_dev	*dev,
				 uint32_t		 id)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	/* We don't want anyone writing to the list whilst we do this */
	list_for_each_entry_safe(ptr, next, &dev->open_files_list, list) {
		if (id == ptr->id) {
			printk(KERN_DEBUG "kbus: Forgetting 'open file' id %u\n",
			       id);
			/* So remove it from our list */
			list_del(&ptr->list);
			/* But *we* mustn't free the actual datastructure! */
			return 0;
		}
	}
	printk(KERN_DEBUG "kbus: Could not find 'open file' %u\n",id);
	return -EINVAL;
}

/*
 * Forget all our "open file" remembrances.
 *
 * Assumed to be called because the device is closing, and thus doesn't lock.
 */
static void kbus_forget_all_open_files(struct kbus_dev	*dev)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	list_for_each_entry_safe(ptr, next, &dev->open_files_list, list) {
		printk(KERN_DEBUG "kbus: Forgetting 'open file' id %u\n",
		       ptr->id);
		/* So remove it from our list */
		list_del(&ptr->list);
		/* But *we* mustn't free the actual datastructure! */
	}
	return;
}

static int kbus_open(struct inode *inode, struct file *filp)
{
	struct kbus_private_data *priv;
	struct kbus_dev          *dev;

	priv = kmalloc(sizeof(*priv),GFP_KERNEL);
	if (!priv) return -EFAULT;

	/*
	 * Use the official magic to retrieve our actual device data
	 * so we can remember it for other file operations.
	 */
	dev = priv->dev = container_of(inode->i_cdev, struct kbus_dev, cdev);

	if (down_interruptible(&dev->sem)) {
		kfree(priv);
		return -ERESTARTSYS;
	}

	/*
	 * Our file descriptor id ("listener id") needs to be unique for this
	 * device, and thus we want to be carefully inside our lock.
	 *
	 * We shall (for now at least) ignore wrap-around - 32 bits is big
	 * enough that it shouldn't cause non-unique ids in our target
	 * applications.
	 *
	 * Listener id 0 is reserved, and we'll use that (on occasion) to mean
	 * kbus itself.
	 */
	if (dev->next_file_id == 0)
		dev->next_file_id ++;
	priv->id = dev->next_file_id ++;
	priv->message_count = 0;
	priv->last_msg_id = 0;	/* What else could it be... */
	INIT_LIST_HEAD(&priv->message_queue);

	(void) kbus_remember_open_file(dev,priv);

	filp->private_data = priv;

	up(&dev->sem);

	printk(KERN_DEBUG "kbus: /dev/kbus0 opened: %u@%p dev %p\n",
	       priv->id,filp,priv->dev);

	return 0;
}

static int kbus_release(struct inode *inode, struct file *filp)
{
	int	retval1 = 0;
	int	retval2 = 0;
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;

	printk(KERN_DEBUG "kbus: Releasing /dev/kbus0 from %u@%p\n",
	       priv->id,filp);

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	/*
	 * XXX If there are any messages in our queue that we should
	 * XXX be replying to, kbus should really generate some sort
	 * XXX of exception for each, indicating we have gone away...
	 */

	retval1 = kbus_empty_message_queue(priv);
	kbus_forget_my_bindings(dev,priv->id);
	retval2 = kbus_forget_open_file(dev,priv->id);
	kfree(priv);

	up(&dev->sem);

	if (retval1)
		return retval1;
	else
		return retval2;
}

/*
 * Determine the private data for the given listener/replier id.
 *
 * Return NULL if we can't find it.
 */
static struct kbus_private_data
	*kbus_find_private_data(struct kbus_private_data *our_priv,
				struct kbus_dev 	 *dev,
				uint32_t		  id)
{
	struct kbus_private_data *l_priv;
	if (id == our_priv->id) {
		/* Heh, it's us, we know who we are! */
		printk(KERN_DEBUG "kbus: -- Id %u is us\n",id);
		l_priv = our_priv;
	} else {
		/* OK, look it up */
		printk(KERN_DEBUG "kbus: -- Looking up id %u\n",id);
		l_priv = kbus_find_open_file(dev,id);
	}
	return l_priv;
}

/*
 * Actually write to anyone interested in this message.
 *
 * Returns 0 if all goes well, or a negative number on error.
 */
static ssize_t kbus_write_to_recipients(struct kbus_private_data   *priv,
					struct kbus_dev		   *dev,
					struct kbus_message_struct *msg,
					uint32_t		    msg_len,
					char			   *name_p,
					uint32_t		   *data_p)
{
	ssize_t		 retval = 0;
	uint32_t	 replier = 0;
	uint32_t	*listeners = NULL;
	int		 num_listeners;
	int		 ii;
	int		 num_sent = 0;	/* # successfully "sent" */
	uint32_t	 old_next_msg_id = dev->next_msg_id;

	/*
	 * Remember that
	 * (a) a listener may occur more than once in our array, and
	 * (b) we have 0 or 1 repliers, but
	 * (c) the replier is *not* one of the listeners.
	 */
	num_listeners = kbus_find_listeners(dev,&listeners,&replier,
					    msg->name_len,name_p);
	if (num_listeners < 0) {
		printk(KERN_DEBUG "kbus/write: Error %d finding listeners\n",
		       num_listeners);
		retval = num_listeners;
		goto done_sending;
	}

	/* Do we have anyone to send our message to? */

	if (num_listeners == 0 && replier == 0 && !msg->in_reply_to) {
		printk(KERN_DEBUG "kbus/write: Not a reply, no listeners, no replier\n");
		retval = -EADDRNOTAVAIL;
		goto done_sending;
	}

	/*
	 * Since we apparently have some listeners, or a replier, or at least
	 * we *are* a reply, it seems safe to assume we're going to be able to
	 * send this message, so (slightly tentatively) assign it a message id.
	 *
	 * We reserve id 0, for use by kbus for synthetic messages.
	 */
	if (dev->next_msg_id == 0)
		dev->next_msg_id ++;
	msg->id = dev->next_msg_id ++;

	/* And we need to add it to the queue for each interested party */

	/*
	 * XXX We *should* check if this is allowed before doing it,
	 * XXX for instance if we've run out of room on one of the
	 * XXX queues, but ignore that for the moment...
	 */

	/*
	 * Remember that kbus_push_message takes a copy of the message for us.
	 *
	 * This is inefficient, since otherwise we could keep a single copy of
	 * the message (or at least the message header) and just bump a
	 * reference count for each "use" of the message,
	 *
	 * However, it also allows us to easily set the "needs a reply" flag
	 * (and associated data) when sending a "needs a reply" message to a
	 * replier, and *unset* the same when sending said message to "just"
	 * listeners...
	 *
	 * Be careful if altering this...
	 */

	/*
	 * If this is a reply message, then we need to send it to the
	 * original sender, irrespective of whether they are a listener or not.
	 */
	if (msg->in_reply_to) {
		struct kbus_private_data *l_priv;
		printk(KERN_DEBUG "kbus/write: Replying to original sender %u\n",
		       msg->to);

		l_priv = kbus_find_private_data(priv,dev,msg->to);
		if (l_priv == NULL) {
			printk(KERN_DEBUG "kbus/write: Can't find sender %u\n",
			       msg->to);
			/* Which sounds fairly nasty */
			goto done_sending;
		}

		retval = kbus_push_message(l_priv,msg_len,msg,true);
		if (retval == 0)
			num_sent ++;
		else
			goto done_sending;
	}

	/* Repliers only get the message if it is marked as wanting a reply. */
	if (replier && KBUS_BIT_WANT_A_REPLY & msg->flags) {
		struct kbus_private_data *l_priv;
		printk(KERN_DEBUG "kbus/write: Considering replier %u\n",
		       replier);

		l_priv = kbus_find_private_data(priv,dev,replier);
		if (l_priv == NULL) {
			printk(KERN_DEBUG "kbus/write: Can't find replier %u\n",
			       replier);
			/* Which sounds fairly nasty */
			goto done_sending;
		}

		retval = kbus_push_message(l_priv,msg_len,msg,true);
		if (retval == 0)
			num_sent ++;
		else
			goto done_sending;
	}

	for (ii=0; ii<num_listeners; ii++) {
		uint32_t  listener = listeners[ii];
		struct kbus_private_data *l_priv;

		printk(KERN_DEBUG "kbus/write: Considering listener %u\n",
		       listener);

		l_priv = kbus_find_private_data(priv,dev,listener);
		if (l_priv == NULL) {
			printk(KERN_DEBUG "kbus/write: Can't find listener %u\n",
			       listener);
			/* Fairly nasty, but maybe it's worth going on... */
			continue;	   /* XXX Review this choice */
		}

		retval = kbus_push_message(l_priv,msg_len,msg,false);
		if (retval == 0)
			num_sent ++;
		else
			goto done_sending;
	}

done_sending:

	if (listeners)
		kfree(listeners);

	if (num_sent == 0) {
		/*
		 * We didn't actually manage to send this message to anyone, so
		 * we can reuse the message id...
		 *
		 * Luckily (!) we remembered what the "next msg id" used to be,
		 * so we don't have to worry about decrementing back over the
		 * unsigned overflow boundary...
		 */
		dev->next_msg_id = old_next_msg_id;
	} else {
		/*
		 * We did send the message, so it becomes the last message we
		 * sent, which can be queried
		 */
		priv->last_msg_id = msg->id;
	}
	return retval;
}

static ssize_t kbus_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *f_pos)
{
	struct kbus_private_data	*priv = filp->private_data;
	struct kbus_dev			*dev = priv->dev;
	struct kbus_message_struct	*msg = NULL;

	ssize_t		 retval;
	uint32_t	 msg_len = 0;
	char		*name_p;
	uint32_t	*data_p;

	printk(KERN_DEBUG "kbus/write: WRITE count %d, pos %d\n",count,(int)*f_pos);

	if (!buf)
		return -EINVAL;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	msg = (struct kbus_message_struct *) buf;

	retval = kbus_dissect_message(msg,&msg_len,&name_p,&data_p);
	if (retval) goto done;

	/* We don't allow sending to wildcards */
	if (name_p[msg->name_len-1] == '*' ||
	    name_p[msg->name_len-1] == '%') {
		retval = -EBADMSG;
		goto done;
	}

	/*
	 * XXX We are insisting a whole message is written in one go.
	 *
	 * This is a significant limitation, and will need fixing.
	 */
	if (count != msg_len) {
		printk(KERN_DEBUG "kbus/write: message length is %u,"
		       " expecting %u from n:%u, d:%u\n",
		       count,msg_len,msg->name_len,msg->data_len);
		retval = -EINVAL;
		goto done;
	}

	/*
	 * Figure out who should receive this message, and write it to them
	 */
	retval = kbus_write_to_recipients(priv,dev,msg,msg_len,name_p,data_p);

done:
	up(&dev->sem);
	if (retval == 0)
		return msg_len;
	else
		return retval;
}

static ssize_t kbus_read(struct file *filp, char __user *buf, size_t count,
			 loff_t *f_pos)
{
	struct kbus_private_data	*priv = filp->private_data;
	struct kbus_dev			*dev = priv->dev;
	struct kbus_message_struct	*msg = NULL;
	ssize_t				 retval = 0;
	uint32_t			 msg_len;

	printk(KERN_DEBUG "kbus/read: READ count %d, pos %d\n",count,(int)*f_pos);

	if (!buf) {
		printk(KERN_ERR "kbus: kbus_read with NULL buf\n");
		return -EINVAL;
	}

	if (!dev) {
		printk(KERN_ERR "kbus: kbus_read with NULL dev\n");
		return -EINVAL;
	}

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	msg = kbus_pop_message(priv);
	if (msg == NULL) {
		printk(KERN_DEBUG "kbus/read: returning 0\n");
		retval = 0;
		goto done;
	}

	msg_len = KBUS_MSG_LEN(msg->name_len,msg->data_len);

	/*
	 * XXX BIG CAVEAT XXX
	 * The following is WRONG - we are not supporting partial reads...
	 * And we will have lost the message (which is REALLY naughty)
	 */

	if (count <  msg_len) {
		printk(KERN_DEBUG "kbus/read: read count is %u,"
		       " message is %u from n:%u, d:%u\n",
		       count,msg_len,msg->name_len,msg->data_len);
		retval = -EINVAL;
		goto done;
	}

	if (copy_to_user(buf, msg, msg_len)) {
		printk(KERN_ERR "kbus/read: error reading from dev %p\n",dev);
		retval = -EFAULT;
		goto done;
	}
	retval = msg_len;

done:
	if (msg) kfree(msg);
	up(&dev->sem);
	return retval;
}

static int kbus_bind(struct kbus_private_data	*priv,
		     struct kbus_dev		*dev,
		     unsigned long		 arg)
{
	int		retval = 0;
	uint32_t	id  = priv->id;
	struct kbus_m_bind_struct *bind;
	char		*name = NULL;

	bind = kmalloc(sizeof(*bind), GFP_KERNEL);
	if (!bind) return -EFAULT;
	if (copy_from_user(bind, (void *)arg, sizeof(*bind))) {
		retval = -EFAULT;
		goto done;
	}

	if (bind->len == 0) {
		printk(KERN_DEBUG "kbus: bind name is length 0\n");
		retval = -EBADMSG;
		goto done;
	} else if (bind->len > KBUS_MAX_NAME_LEN) {
		printk(KERN_DEBUG "kbus: bind name is length %d\n",
		       bind->len);
		retval = -ENAMETOOLONG;
		goto done;
	}

	name = kmalloc(bind->len+1, GFP_KERNEL);
	if (!name) {
		retval = -EFAULT;
		goto done;
	}
	if (copy_from_user(name, bind->name, bind->len)) {
		retval = -EFAULT;
		goto done;
	}
	name[bind->len] = 0;


	if (kbus_bad_message_name(name,bind->len)) {
		retval = -EBADMSG;
		goto done;
	}
	printk(KERN_DEBUG "kbus: Bind request %c %d:'%s' on %u\n",
	       (bind->replier?'R':'L'), bind->len, name, id);
	retval = kbus_remember_binding(dev, id,
				       bind->replier,
				       bind->guaranteed,
				       bind->len,
				       name);
	if (retval == 0) {
		/* The binding will use our copy of the message name */
		name = NULL;
	}
done:
	if (name) kfree(name);
	kfree(bind);
	return retval;
}

static int kbus_unbind(struct kbus_private_data	*priv,
		       struct kbus_dev		*dev,
		       unsigned long		 arg)
{
	int		retval = 0;
	uint32_t	id  = priv->id;
	struct kbus_m_bind_struct *bind;
	char		*name = NULL;

	bind = kmalloc(sizeof(*bind), GFP_KERNEL);
	if (!bind) return -EFAULT;
	if (copy_from_user(bind, (void *)arg, sizeof(*bind))) {
		retval = -EFAULT;
		goto done;
	}

	if (bind->len == 0) {
		printk(KERN_DEBUG "kbus: unbind name is length 0\n");
		retval = -EBADMSG;
		goto done;
	} else if (bind->len > KBUS_MAX_NAME_LEN) {
		printk(KERN_DEBUG "kbus: unbind name is length %d\n",
		       bind->len);
		retval = -ENAMETOOLONG;
		goto done;
	}

	name = kmalloc(bind->len+1, GFP_KERNEL);
	if (!name) {
		retval = -EFAULT;
		goto done;
	}
	if (copy_from_user(name, bind->name, bind->len)) {
		retval = -EFAULT;
		goto done;
	}
	name[bind->len] = 0;

	if (kbus_bad_message_name(name,bind->len)) {
		retval = -EBADMSG;
		goto done;
	}
	printk(KERN_DEBUG "kbus: Unbind request %c %d:'%s' on %u\n",
	       (bind->replier?'R':'L'), bind->len, name, id);
	retval = kbus_forget_binding(dev, id,
				     bind->replier,
				     bind->guaranteed,
				     bind->len,
				     name);
done:
	if (name) kfree(name);
	kfree(bind);
	return retval;
}

static int kbus_replier(struct kbus_private_data	*priv,
			struct kbus_dev			*dev,
			unsigned long			 arg)
{
	int		retval = 0;
	uint32_t	id  = priv->id;
	struct kbus_m_bind_query_struct *query;
	char 		*name = NULL;

	query = kmalloc(sizeof(*query), GFP_KERNEL);
	if (!query) return -EFAULT;
	if (copy_from_user(query, (void *)arg, sizeof(*query))) {
		retval = -EFAULT;
		goto done;
	}

	if (query->len == 0 || query->len > KBUS_MAX_NAME_LEN) {
		printk(KERN_DEBUG "kbus: Replier name is length %d\n",query->len);
		retval = -ENAMETOOLONG;
		goto done;
	}

	name = kmalloc(query->len+1, GFP_KERNEL);
	if (!name) {
		retval = -EFAULT;
		goto done;
	}
	if (copy_from_user(name, query->name, query->len)) {
		retval = -EFAULT;
		goto done;
	}
	name[query->len] = 0;

	printk(KERN_DEBUG "kbus: Bindas request %d:'%s' on %u\n",
	       query->len, name, id);
	retval = kbus_find_replier(dev,
				   &query->return_id,
				   query->len,
				   name);
	if (retval) goto done;
	/*
	 * Copy the whole structure back, rather than try to work out (in a
	 * guaranteed-safe manner) where the 'id' actually lives
	 */
	if (copy_to_user((void *)arg, query, sizeof(*query))) {
	    retval = -EFAULT;
	    goto done;
	}
done:
	if (name) kfree(name);
	kfree(query);
	return retval;
}

#if 0
static int kbus_nextmsg(struct kbus_private_data	*priv,
			struct kbus_dev			*dev,
			unsigned long			 arg)
{
	int	retval = 0;
	struct kbus_message_struct *msg;

	/* If we were partway through a message, lose it */
	if (priv->read_msg) {
		kfree(priv->read_msg);
		priv->read_msg = NULL;
		priv->read_msg_len = 0;
		priv->read_msg_pos = 0;
	}

	/* Have we got a next message? */
	msg = kbus_pop_message(priv);
	if (msg == NULL) {
		/*
		 * A return value of 0 means no message, and that's
		 * what __put_user returns for success.
		 */
		return __put_user(priv->read_msg_len, (uint32_t __user *)arg);
	}
	priv->read_msg = (char *)msg;
	priv->read_msg_len = KBUS_MSG_LEN(msg->name_len,msg->data_len);
	priv->read_msg_pos = 0;

	retval = __put_user(priv->read_msg_len, (uint32_t __user *)arg);
	if (retval)
		return retval;
	else
		return 1;	/* We had a message */
}
#else
// The original meaning of the NEXTMSG ioctl, for the moment
static int kbus_nextmsg(struct kbus_private_data	*priv,
			struct kbus_dev			*dev,
			unsigned long			 arg)
{
	int	retval = 0;
	int	len;

	len = kbus_next_message_len(priv);
	if (len < 0)
	{
		retval = len;
	} else {
		retval = __put_user(len, (uint32_t __user *)arg);
	}
	return retval;
}
#endif

#include <linux/ioctl.h>
#define KBUS_IOC_MAGIC	'k'	/* 0x6b - which seems fair enough for now */
#define KBUS_IOC_RESET	  _IO(  KBUS_IOC_MAGIC,  1)
#define KBUS_IOC_BIND	  _IOW( KBUS_IOC_MAGIC,  2, char *)
#define KBUS_IOC_UNBIND	  _IOW( KBUS_IOC_MAGIC,  3, char *)
#define KBUS_IOC_BOUNDAS  _IOR( KBUS_IOC_MAGIC,  4, char *)
#define KBUS_IOC_REPLIER  _IOWR(KBUS_IOC_MAGIC,  5, char *)
#define KBUS_IOC_NEXTMSG  _IOR( KBUS_IOC_MAGIC,  6, char *)
#define KBUS_IOC_LENLEFT  _IOR( KBUS_IOC_MAGIC,  7, char *)
#define KBUS_IOC_SEND	  _IO(  KBUS_IOC_MAGIC,  8)
#define KBUS_IOC_DISCARD  _IO(  KBUS_IOC_MAGIC,  9)
#define KBUS_IOC_LASTSENT _IOR( KBUS_IOC_MAGIC, 10, char *)
/* XXX If adding another IOCTL, remember to increment the next number! XXX */
#define KBUS_IOC_MAXNR	10

static int kbus_ioctl(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	int err = 0;
	int retval = 0;
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;
	uint32_t	 id  = priv->id;

	printk(KERN_DEBUG "kbus: ioctl %08x arg %08lx\n", cmd, arg);

	if (_IOC_TYPE(cmd) != KBUS_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > KBUS_IOC_MAXNR) return -ENOTTY;
	/*
	 * Check our arguments at least vaguely match. Note that VERIFY_WRITE
	 * allows R/W transfers. Remember that 'type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and "write"
	 * is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	switch (cmd) {

	case KBUS_IOC_RESET:
		/* This is currently a no-op, but may be useful later */
		printk(KERN_DEBUG "kbus: reset\n");
		break;

	case KBUS_IOC_BIND:
		/*
		 * BIND: indicate that a file wants to receive messages of a
		 * given name
		 */
		retval = kbus_bind(priv,dev,arg);
		break;

	case KBUS_IOC_UNBIND:
		/*
		 * UNBIND: indicate that a file no longer wants to receive
		 * messages of a given name
		 */
		retval = kbus_unbind(priv,dev,arg);
		break;

	case KBUS_IOC_BOUNDAS:
		/*
		 * What is the "binding id" for this file descriptor
		 * (this "listener")?
		 */
		printk(KERN_DEBUG "kbus: %p bound as %u\n",filp,id);
		retval = __put_user(id, (uint32_t __user *)arg);
		break;

	case KBUS_IOC_REPLIER:
		/*
		 * Who (if anyone) is bound to reply to this message?
		 * arg in: message name
		 * arg out: listener id
		 * return: 0 means no-one, 1 means someone
		 * We can't just return the id as the return value of the ioctl,
		 * because it's an unsigned int, and the ioctl return must be
		 * signed...
		 */
		retval = kbus_replier(priv, dev, arg);
		break;

	case KBUS_IOC_NEXTMSG:
		/*
		 * What is the length of the next message queued for this
		 * file descriptor (0 if no next message)
		 *
		 * XXX TO BECOME:
		 * Get the next message ready to be read, and return its
		 * length.
		 *
		 * arg in:  none
		 * arg out: number of bytes in next message
		 * retval:  0 if no next message, 1 if there is a next message,
		 *          negative value if there's an error.
		 */
		retval = kbus_nextmsg(priv, dev, arg);
		break;

	case KBUS_IOC_LENLEFT:
		/* How many bytes are left to read in the current message? */
		{
			uint32_t left = priv->read_msg_len - priv->read_msg_pos;
			printk(KERN_DEBUG "kbus: LENLEFT %d\n",retval);
			retval = __put_user(left, (uint32_t __user *)arg);
		}
		break;

	case KBUS_IOC_SEND:
		/* Send the curent message, we've finished writing it. */
		printk(KERN_DEBUG "kbus: SEND\n");
		break;

	case KBUS_IOC_DISCARD:
		/* Throw away the message we're currently writing. */
		printk(KERN_DEBUG "kbus: DISCARD\n");
		break;

	case KBUS_IOC_LASTSENT:
		/*
		 * What was the message id of the last message written to this
		 * file descriptor? Before any messages have been written to this
		 * file descriptor, this ioctl will return 0.
		 */
		printk(KERN_DEBUG "kbus: %p last message id %u\n",filp,priv->last_msg_id);
		retval = __put_user(priv->last_msg_id, (uint32_t __user *)arg);
		break;

	default:  /* *Should* be redundant, if we got our range checks right */
		return -ENOTTY;
	}

	up(&dev->sem);
	return retval;
}

/* File operations for /dev/kbus0 */
struct file_operations kbus_fops = {
	.owner =    THIS_MODULE,
	.read =     kbus_read,
	.write =    kbus_write,
	.ioctl =    kbus_ioctl,
	.open =     kbus_open,
	.release =  kbus_release,
};

static void kbus_setup_cdev(struct kbus_dev *dev, int devno)
{
	int err;

	/*
	 * Remember to initialise the mutex *before* making the device
	 * available!
	 */
	init_MUTEX(&dev->sem);

	/*
	 * This seems like a sensible place to setup other device specific
	 * stuff, too.
	 */
	INIT_LIST_HEAD(&dev->bound_message_list);
	INIT_LIST_HEAD(&dev->open_files_list);

	dev->next_file_id = 0;
	dev->next_msg_id = 0;

	cdev_init(&dev->cdev, &kbus_fops);
	dev->cdev.owner = THIS_MODULE;

	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		printk(KERN_ERR "Error %d adding kbus0 as a character device\n",err);
}

static void kbus_teardown_cdev(struct kbus_dev *dev)
{
	cdev_del(&dev->cdev);

	kbus_forget_all_bindings(dev);
	kbus_forget_all_open_files(dev);
}

/* ========================================================================= */
/* PROC */
#include <linux/proc_fs.h>

static struct proc_dir_entry *kbus_proc_dir;
static struct proc_dir_entry *kbus_proc_file_bindings;

/*
 * A quick and dirty way of reporting on the current bindings. We assume that
 * we can fit all of our information on one page of output.
 */
static int kbus_read_proc_bindings(char *buf, char **start, off_t offset,
				   int count, int *eof, void *data)
{
	int ii;
	int len = 0;
	int limit = count - 4;		/* Leaving room for "...\n" */

	/* We report on all of the KBUS devices */
	for (ii=0; ii<kbus_num_devices; ii++) {
		struct kbus_dev	*dev = &kbus_devices[ii];

		struct kbus_message_binding *ptr;
		struct kbus_message_binding *next;

		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;

		list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
			if (len + 4 + 10 + 3 + 3 +
			    strlen(ptr->name) + 1 < limit)
			{
				len += sprintf(buf+len,
					       "%2d: %1u %c %c %s\n",
					       ii,
					       ptr->bound_to,
					       (ptr->replier?'R':'L'),
					       (ptr->guaranteed?'T':'F'),
					       ptr->name);
			} else {
				/* Icky trick to indicate we didn't finish */
				len += sprintf(buf+len,"...\n");
			}
		}

		up(&dev->sem);
		*eof = 1;
	}
	return len;
}

/* ========================================================================= */

static int __init kbus_init(void)
{
	int   result;
	int   ii;
	dev_t devno = 0;

	printk(KERN_NOTICE "Initialising kbus module (%d device%s)\n",
	       kbus_num_devices, kbus_num_devices==1?"":"s");
	printk(KERN_NOTICE "========================\n"); /* XXX Naughty me */
	/* XXX The above underlining is to allow me to see transitions in */
	/* XXX the dmesg output (obviously) */

	if (kbus_num_devices < MIN_NUM_DEVICES ||
	    kbus_num_devices > MAX_NUM_DEVICES) {
		printk(KERN_ERR "kbus: requested number of devices %d not %d..%d\n",
		       kbus_num_devices,MIN_NUM_DEVICES,MAX_NUM_DEVICES);
		return -EINVAL;
	}

	/* ================================================================= */
	/*
	 * Our main purpose is to provide /dev/kbus
	 * We are happy to start our device numbering with device 0
	 */
	result = alloc_chrdev_region(&devno, kbus_minor, kbus_num_devices, "kbus");
	/* We're quite happy with dynamic allocation of our major number */
	kbus_major = MAJOR(devno);
	if (result < 0) {
		printk(KERN_WARNING "kbus: Cannot allocate character device region\n");
		return result;
	}

	kbus_devices = kmalloc(kbus_num_devices * sizeof(struct kbus_dev),
			       GFP_KERNEL);
	if (!kbus_devices) {
		printk(KERN_WARNING "kbus: Cannot allocate devices\n");
		unregister_chrdev_region(devno, kbus_num_devices);
		return -ENOMEM;
	}
	memset(kbus_devices, 0, kbus_num_devices * sizeof(struct kbus_dev));

	for (ii=0; ii<kbus_num_devices; ii++)
	{
		/* Connect the device up with its operations */
		devno = MKDEV(kbus_major,kbus_minor+ii);
		kbus_setup_cdev(&kbus_devices[ii],devno);
	}

	/* ================================================================= */
	/* +++ NB: before kernel 2.6.13, the functions below were
	 * +++ "simple_class_create" and "simple_class_device_add". They are
	 * +++ documented as such in Linux Device Drivers, 3rd edition. When it
	 * +++ became clear that everyone was using the "simple" API, the kernel code
	 * +++ was refactored to make that the norm.
	 */

	/*
	 * To make the user's life as simple as possible, let's make our device
	 * hot pluggable -- this means that on a modern system it *should* just
	 * appear, as if by magic (and go away again when the module is removed).
	 */
	kbus_class_p = class_create(THIS_MODULE, "kbus");
	if (IS_ERR(kbus_class_p)) {
		long err = PTR_ERR(kbus_class_p);
		if (err == -EEXIST) {
			printk(KERN_WARNING "kbus: Cannot create kbus class, it already exists\n");
		} else {
			printk(KERN_ERR "kbus: Error creating kbus class\n");
			unregister_chrdev_region(devno, kbus_num_devices);
			return err;
		}
	}
	/*
	 * Whilst we only want one device at the moment, name it "kbus0" in case we
	 * decide we're wrong later on.
	 */

	kbus_class_devices = kmalloc(kbus_num_devices * sizeof(*kbus_class_devices),
				     GFP_KERNEL);
	if (!kbus_class_devices)
	{
		printk(KERN_ERR "kbus: Error creating kbus class device array\n");
		unregister_chrdev_region(devno, kbus_num_devices);
		class_destroy(kbus_class_p);
		/* XXX Is this enough tidying up? CHECK XXX */
		return -ENOMEM;
	}

	for (ii=0; ii<kbus_num_devices; ii++) {
		dev_t this_devno = MKDEV(kbus_major,kbus_minor+ii);
		kbus_class_devices[ii] = device_create(kbus_class_p, NULL,
						       this_devno, NULL, "kbus%d", ii);
	}

	/* ================================================================= */
	/*
	 * Our subsidiary purpose is to allow the user to find things out
	 * about us via /proc/kbus
	 */
	kbus_proc_dir = proc_mkdir("kbus", NULL);
	if (kbus_proc_dir) {
		kbus_proc_file_bindings =
		       	create_proc_read_entry("bindings", 0, kbus_proc_dir,
					       kbus_read_proc_bindings, NULL);
	}

	return 0;
}

static void __exit kbus_exit(void)
{
	/* No locking done, as we're standing down */

	int   ii;
	dev_t devno = MKDEV(kbus_major,kbus_minor);

	printk(KERN_NOTICE "Standing down kbus module\n");

	/*
	 * If I'm destroying the class, do I actually need to destroy the
	 * individual device therein first? Best safe...
	 */
	for (ii=0; ii<kbus_num_devices; ii++) {
		dev_t this_devno = MKDEV(kbus_major,kbus_minor+ii);
		device_destroy(kbus_class_p, this_devno);
	}
	class_destroy(kbus_class_p);

	for (ii=0; ii<kbus_num_devices; ii++) {
		kbus_teardown_cdev(&kbus_devices[ii]);
	}
	unregister_chrdev_region(devno, kbus_num_devices);

	if (kbus_proc_dir) {
		if (kbus_proc_file_bindings)
			remove_proc_entry("bindings",kbus_proc_dir);
		remove_proc_entry("kbus",NULL);
	}
}

module_param(kbus_num_devices, int, S_IRUGO);
module_init(kbus_init);
module_exit(kbus_exit);

MODULE_DESCRIPTION("KBUS lightweight messaging system");
MODULE_AUTHOR("tibs@tibsnjoan.co.uk, tony.ibbs@gmail.com");
/*
 * All well-behaved Linux kernel modules should be licensed under GPL v2.
 * So shall it be.
 *
 * (According to the comments in <linux/module.h>, the "v2" is implicit here)
 *
 * We also license under the MPL, to allow free use outwith Linux is anyone
 * wishes.
 */
MODULE_LICENSE("Dual MPL/GPL");


// Kernel style layout -- note that including this text contravenes the Linux
// coding style, and is thus a Bad Thing. Expect these lines to be removed if
// this ever gets added to the kernel distribution.
// Local Variables:
// c-set-style: "linux"
// End:
// vim: set tabstop=8 shiftwidth=8 noexpandtab:
