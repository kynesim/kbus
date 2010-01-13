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
#include <linux/ctype.h>	/* for isalnum */
#include <linux/poll.h>
#include <linux/sched.h>	/* for current->pid */
#include <asm/uaccess.h>	/* copy_*_user() functions */
#include <asm/page.h>		/* PAGE_SIZE */

#include "kbus_defns.h"

/*
 * KBUS can support multiple devices, as /dev/kbus<N>. These all have
 * the same major device number, and map to differing minor device
 * numbers. It may or may not be a surprise that <N> just happens to be
 * the minor device number as well (but don't use that  information for
 * anything).
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
 * We need to set a maximum number of KBUS devices (corrsponding to a limit on
 * minor device numbers). The obvious limit (corresponding to what we'd have
 * got if we used the deprecated "register_chrdev" to setup our device) is 256,
 * so we'll go with that.
 */
#define MIN_NUM_DEVICES		  1
#define MAX_NUM_DEVICES		256
#define DEF_NUM_DEVICES		  1

/* By default, enable verbose debug to be selected */
#define VERBOSE_DEBUG 1

static int  kbus_num_devices = DEF_NUM_DEVICES;

/* Who we are -- devices */
static int  kbus_major = 0;             /* We'll go for dynamic allocation */
static int  kbus_minor = 0;             /* We're happy to start with device 0 */

static struct class      *kbus_class_p;
static struct device    **kbus_class_devices;

/* We need a way of remembering message bindings */
struct kbus_message_binding {
	struct list_head list;
	struct kbus_private_data *bound_to;	/* who we're bound to */
	uint32_t		  bound_to_id;	/* but the id is often useful */
	uint32_t		  is_replier;	/* bound as a replier */
	uint32_t		  name_len;
	char			 *name;		/* the message name */
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
	uint32_t		 count;		/* Number of entries in use */
	uint32_t		 size;		/* Actual size of the array */
	uint32_t		 max_count;	/* Max 'count' we've had */
	/*
	 * An array is probably the worst way to store a list of message ids,
	 * but it's *very simple*, and should work OK for a smallish number of
	 * message ids. So it's a place to start...
	 *
	 * Note the array may have "unused" slots, signified by message id {0:0}
	 */
	struct kbus_msg_id	*ids;
};

/* An item in the list of requests that a KSock has not yet replied to */
struct kbus_unreplied_item {
	struct list_head 	 list;
	struct kbus_msg_id	 id;		/* the request's id */
	uint32_t		 from;		/* the sender's id */
	uint32_t		 name_len;	/* and its name... */
	char			*name;
};

/*
 * Stuff to do with the user reading a message (using 'read')
 *
 * The message (as written to the user) is made up of:
 *
 * 0. the message header
 * 1. the message name
 * 2. padding to bring that up to a NULL terminator and then a 4-byte boundary
 * 3. the message data (if any)
 * 4. padding to bring that up to a 4-byte boundary
 * 5. the final end guard
 */

/*
 * The minimum number of parts is 4:
 *
 * * the message header
 * * the message name
 * * padding to bring that up to a NULL terminator and then a 4-byte boundary.
 * * and, at the end, the final end guard.
 *
 * If we have message data, then (before the final end guard) we also have N
 * data parts, plus padding to bring the total up to a 4-byte boundary.
 */
#define KBUS_MIN_NUM_PARTS	4

#define KBUS_PART_HDR		0
#define KBUS_PART_NAME		1
#define KBUS_PART_NPAD		2
#define KBUS_PART_DATA_START	3

/* We can't need more than 8 characters of padding, by definition! */
static char		*static_zero_padding = "\0\0\0\0\0\0\0\0";
static uint32_t		 static_end_guard = KBUS_MSG_END_GUARD;

/*
 * Note that:
 *
 * 1. 'hdr' and 'from[0]' are both the message header.
 * 2. 'parts[1]' is the message name
 * 3. 'parts[2..]' are the message data, if any, and 'data'
 *    is the *reference counted* data (again, if any)
 * 4. 'parts[-1]' is the (static) final end guard
 *
 * Beware that, at some point in writing [1]_, the 'name' and 'data'
 * pointers in the message header will get unset, since we (a) want
 * to copy the header out byte-by-byte, and (b) we're trying to write
 * out an "entire" message, so we need them unset. This means that,
 * after that point, any attempt to treat the message header *as* a
 * message header will fail.
 *
 * .. [1] Strangely enough, in kbus_write().
 */
struct kbus_read_msg {
	struct kbus_message_header	 *hdr;
	struct kbus_data_ptr		 *data;
	int				  num_parts;
	char				**parts;  /* ARRAY */
	unsigned			 *lengths;/* ARRAY */
	int				  which;/* The current item */
	uint32_t			  pos;	/* How far they've read in it */
};

/*
 * This is the data for an individual KSock
 *
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
	struct list_head	 list;
	struct kbus_dev		*dev;		 /* Which device we are on */
	uint32_t	 	 id;		 /* Our own id */
	struct kbus_msg_id	 last_msg_id_sent;  /* As it says - see above */
	uint32_t		 message_count;	 /* How many messages for us */
	uint32_t		 max_messages;	 /* How many messages allowed */
	struct list_head	 message_queue;	 /* Messages for us */

	/*
	 * It's useful (for /proc/kbus/bindings) to remember the PID of the
	 * current process
	 */
	pid_t			 pid;

	/* Wait for something to appear in the message_queue */
	wait_queue_head_t	 read_wait;

	/* The message currently being read by the user */
	struct kbus_read_msg	 read;

	/* The message currently being written by the user */
	char		*write_msg;
	size_t		 write_msg_size;	/* The buffer size */
	size_t		 write_msg_len;		/* How much they've written */

	/* Are we currently sending that message? */
	int		 sending;

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
	struct kbus_msg_id_mem	outstanding_requests;

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
	 *     XXX Of course, when messages get reference counted internally,
	 *     XXX and message names are also available as message codes,
	 *     XXX we should be able to store *just* the message id, code
	 *     XXX and from values, which is considerably shorter.
	 *
	 * XXX Do I need to limit the size of this list? *Can* it grow
	 * XXX in an unbounded manner? The limitation is set by the ability
	 * XXX of the sender(s) to send requests, which in turn is limited
	 * XXX by the number of slots they can reserve for the replies to
	 * XXX those requests, in their own message queues. 
	 * XXX
	 * XXX If I do impose a limit, then I would also need to stop a
	 * XXX sender sending a request because the replier has too many
	 * XXX replies outstanding (which sounds like it might have gone
	 * XXX to sleep). On the other hand, in that case we would also
	 * XXX assume it's not reponding to messages in general, and so
	 * XXX it's message queue would fill up, and I *think* that's a
	 * XXX sufficient condition.
	 */
	struct list_head	replies_unsent;
	uint32_t		num_replies_unsent;
	uint32_t		max_replies_unsent;

	/*
	 * XXX ---------------------------------------------------------- XXX
	 * Discussion: We need to police replying, such that a replier
	 * may only reply to requests that it has received (where "received"
	 * means "had placed into its message queue", because KBUS must reply
	 * for us if the particular KSock is not going to).
	 *
	 * It is possible to do this using either the 'outstanding_requests'
	 * or the 'replies_unsent' list.
	 *
	 * Using the 'outstanding_requests' list means that when a replier
	 * wants to send a reply, it needs to look up who the original-sender
	 * is (from its KSock id, in the "from" field of the message), and
	 * check against that. This is a bit inefficient.
	 *
	 * Using the 'replies_unsent' list means that when a replier wants
	 * to send a reply, it just needs to find the right message stub
	 * in said 'replies_unsent' list, and check that the reply *does*
	 * match the original request. This may be more efficient, depending.
	 *
	 * The 'outstanding_requests' list is currently used, simply because
	 * it was implemented first.
	 * XXX ---------------------------------------------------------- XXX
	 */

	/*
	 * By default, if a KSock binds to a message name as both Replier and
	 * Listener (typically by binding to a specific message name as Replier
	 * and to a wildcard including it as Listener), and a Reqest of that
	 * name is sent to that KSock, it will get the message once as Replier
	 * (marked "WANT_YOU_TO_REPLY"), and once as listener.
	 * 
	 * This is technically sensible, but can be irritating to the end user
	 * who really often only wants to receive the message once.
	 *
	 * If "messages_only_once" is set, then when a message is about to be
	 * put onto a KSocks message queue, it will only be added if it (i.e.,
	 * a message with the same id) has not already just been added. This
	 * is safe because Requests to the specific Replier are always dealt
	 * with first.
	 *
	 * As a side-effect, which I think also makes sense, this will also
	 * mean that if a Listener has bound to the same message name multiple
	 * times (as a Listener), then they will only get the message once.
	 */
	int			 messages_only_once;
	/*
	 * Messages can be added to either end of our message queue (i.e.,
	 * depending on whether they're urgent or not). This means that the
	 * "only once" mechanism needs to check both ends of the queue (which
	 * is a pain). Or we can just remember the message id of the last
	 * message pushed onto the queue. Which is much simpler.
	 */
	struct kbus_msg_id	 msg_id_just_pushed;
};

/* What is a sensible number for the default maximum number of messages? */
#define DEF_MAX_MESSAGES	100

/* Information belonging to each /dev/kbus<N> device */
struct kbus_dev
{
	struct cdev   		cdev;	/* Character device data */

	uint32_t		index;	/* Which /dev/kbus<n> device we are */

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
	 * The actual KSock entries (one per 'open("/dev/kbus0")') 
	 * This is to allow us to find the 'kbus_private_data' instances,
	 * so that we can get at all the message queues. The details of
	 * how we do this are *definitely* going to change...
	 */
	struct list_head	open_ksock_list;

	/* Has one of our KSocks made space available in its message queue? */
	wait_queue_head_t	 write_wait;

	/*
	 * Each open file descriptor needs an internal id - this is used
	 * when binding messages to listeners, but is also needed when we
	 * want to reply. We reserve the id 0 as a special value ("none").
	 */
	uint32_t		next_file_id;

	/*
	 * Every message sent has a unique id (again, unique per device).
	 */
	uint32_t		next_msg_serial_num;

	/* Are we wanting debugging messages? */
	uint32_t		verbose;

	/*
	 * Are we wanting to send a synthetic message for each Replier
	 * bind/unbind? */
	uint32_t		report_replier_binds;
};

/* Our actual devices, 0 through kbus_num_devices-1 */
static struct kbus_dev       **kbus_devices;

/*
 * Each entry in a message queue holds a single message.
 */
struct kbus_message_queue_item {
	struct list_head		 list;
	struct kbus_message_header	*msg;
};

/*
 * A reference counting wrapper for message data
 *
 * If 'as_pages' is false, then the data is stored as a single kmalloc'd
 * entity, pointed to by 'parts[0]', and of length 'lengths[0]'. If 'as_pages'
 * is false, 'num_parts' will always be 1.
 *
 * If 'as_pages' is true, then the data is stored as 'num_parts' pages, each
 * pointed to by 'parts[n]', and of length 'lengths[n]' (clearly, all but the
 * last will have the same size).
 *
 * 'refcnt' is then the reference count for this data - when it
 * reaches 0, everything (the parts, the arrays and the datastructure)
 * gets freed.
 */
struct kbus_data_ptr {
	int		 as_pages;
	unsigned	 num_parts;
	unsigned long	*parts;
	unsigned	*lengths;
	int		 refcnt;
};

/* ========================================================================= */
/* This is to allow *me* to see where the definitions end and code starts    */

/* As few foreshadowings as I can get away with */
static struct kbus_private_data *kbus_find_open_ksock(struct kbus_dev	*dev,
						     uint32_t		 id);

static void kbus_discard(struct kbus_private_data	*priv);

/* I really want this function where it is in the code, so need to foreshadow */
static int kbus_setup_new_device(int which);
/* ========================================================================= */

/*
 * Wrap a set of data pointers and lengths in a reference
 */
static struct kbus_data_ptr *kbus_new_data_ref(struct kbus_private_data *priv,
					       int		         as_pages,
					       unsigned			 num_parts,
					       unsigned long		*parts,
					       unsigned			*lengths)
{
	struct kbus_data_ptr *new = NULL;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new) return NULL;

	new->as_pages  = as_pages;
	new->parts     = parts;
	new->lengths   = lengths;
	new->num_parts = num_parts;
	new->refcnt    = 1;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u <00 ref %p now %d>\n",
		       priv->dev->index,priv->id,
		       new->parts,new->refcnt);
	}
#endif
	return new;
}

/*
 * Increment the reference count for our pointer.
 *
 * Returns the (same) reference, for convenience.
 */
static struct kbus_data_ptr *kbus_ref_data(struct kbus_private_data	*priv,
					   struct kbus_data_ptr		*ref)
{
	if (ref != NULL) {
		ref->refcnt ++;
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   %u/%u <UP ref %p now %d>\n",
			       priv->dev->index,priv->id,
			       ref->parts,ref->refcnt);
		}
#endif
	}
	return ref;
}

/*
 * Forget a reference to our pointer, and if no-one cares anymore, free it and
 * its contents.
 */
static void kbus_deref_data(struct kbus_private_data	*priv,
			    struct kbus_data_ptr	*ref)
{
	if (ref == NULL)
		return;

	ref->refcnt --;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u <DN ref %p now %d>\n",
		       priv->dev->index,priv->id,
		       ref->parts,ref->refcnt);
	}
#endif

	if (ref->refcnt == 0) {
		if (ref->parts == NULL) {
			printk(KERN_ERR "kbus: %u/%u Removing reference, but ptr already freed\n",
			       priv->dev->index,priv->id);
		} else {
			int jj;
			if (ref->as_pages)
				for (jj=0; jj<ref->num_parts; jj++)
					free_page((unsigned long)ref->parts[jj]);
			else
				for (jj=0; jj<ref->num_parts; jj++)
					kfree((void *)ref->parts[jj]);
			kfree(ref->parts);
			kfree(ref->lengths);
			ref->parts = NULL;
			ref->lengths = NULL;
		}
		kfree(ref);
	 }
}

/*
 * Return a stab at the next size for an array
 */
static uint32_t kbus_next_size(uint32_t		old_size)
{
	if (old_size < 16)
		/* For very small numbers, just double */
		return old_size << 1;
	else 
		/*
		 * Otherwise, try something like the mechanism used for Python
		 * lists - doubling feels a bit over the top
		 */
		return old_size + (old_size >> 3);
}

static int kbus_same_message_id(struct kbus_msg_id 	*msg_id,
				uint32_t		 network_id,
				uint32_t		 serial_num)
{
	return msg_id->network_id == network_id &&
		msg_id->serial_num == serial_num;
}

static int kbus_init_msg_id_memory(struct kbus_private_data	*priv)
{
#define INIT_MSG_ID_MEMSIZE	16

	struct kbus_msg_id_mem	*mem = &priv->outstanding_requests;
	struct kbus_msg_id	*ids;

	ids = kmalloc(sizeof(*ids)*INIT_MSG_ID_MEMSIZE, GFP_KERNEL);
	if (!ids) return -ENOMEM;

	memset(ids, 0, sizeof(*ids)*INIT_MSG_ID_MEMSIZE);

	mem->count = 0;
	mem->max_count = 0;
	mem->ids = ids;
	mem->size = INIT_MSG_ID_MEMSIZE;
	return 0;
}

static void kbus_empty_msg_id_memory(struct kbus_private_data	*priv)
{
	struct kbus_msg_id_mem	*mem = &priv->outstanding_requests;

	if (mem->ids == NULL)
		return;

	kfree(mem->ids);
	mem->ids = NULL;
	mem->size = 0;
	mem->max_count = 0;
	mem->count = 0;
}

/*
 * Note we don't worry about whether the id is already in there - if
 * the user cares, that's up to them (I don't think I do)
 */
static int kbus_remember_msg_id(struct kbus_private_data	*priv,
				struct kbus_msg_id		*id)
{
	struct kbus_msg_id_mem	*mem = &priv->outstanding_requests;
	int ii, which;
#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Remembering outstanding"
		       " request %u:%u (count->%d)\n",
		       priv->dev->index,priv->id,
		       id->network_id,id->serial_num,mem->count+1);
	}
#endif


	/* First, try for an empty slot we can re-use */
	for (ii=0; ii<mem->size; ii++) {
		if (kbus_same_message_id(&mem->ids[ii],0,0)) {
			which = ii;
			goto done;
		}

	}
	/* Otherwise, give in and use a new one */
	if (mem->count == mem->size) {
		uint32_t old_size = mem->size;
		uint32_t new_size = kbus_next_size(old_size);
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   %u/%u XXX outstanding"
			       " request array size %u -> %u\n",
			       priv->dev->index,priv->id,
			       old_size, new_size);
		}
#endif

		mem->ids = krealloc(mem->ids,
				    sizeof(struct kbus_msg_id) * new_size,
				    GFP_KERNEL);
		if (!mem->ids) return -EFAULT;
		/* XXX Should probably be a memset or somesuch */
		for (ii=old_size; ii<new_size; ii++) {
			mem->ids[ii].network_id = 0;
			mem->ids[ii].serial_num = 0;
		}
		mem->size = new_size;
		which = mem->count;
	}
	which = mem->count;
done:
	mem->ids[which] = *id;
	mem->count ++;
	if (mem->count > mem->max_count)
		mem->max_count = mem->count;
	return 0;
}

/* Returns 0 if we found it, -1 if we couldn't find it */
static int kbus_find_msg_id(struct kbus_private_data	*priv,
		            struct kbus_msg_id		*id)
{
	struct kbus_msg_id_mem	*mem = &priv->outstanding_requests;
	int ii;
	for (ii=0; ii<mem->size; ii++) {
		if (kbus_same_message_id(&mem->ids[ii],
					 id->network_id,id->serial_num)) {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   %u/%u Found outstanding request %u:%u (count=%d)\n",
				       priv->dev->index,priv->id,
				       id->network_id,id->serial_num,mem->count);
			}
#endif
			return 0;
		}
	}
#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Could not find outstanding request %u:%u (count=%d)\n",
		       priv->dev->index,priv->id,
		       id->network_id,id->serial_num,mem->count);
	}
#endif
	return -1;
}

/* Returns 0 if we found and forgot it, -1 if we couldn't find it */
static int kbus_forget_msg_id(struct kbus_private_data	*priv,
			      struct kbus_msg_id	*id)
{
	struct kbus_msg_id_mem	*mem = &priv->outstanding_requests;
	int ii;
	for (ii=0; ii<mem->size; ii++) {
		if (kbus_same_message_id(&mem->ids[ii],
					 id->network_id,id->serial_num)) {
			mem->ids[ii].network_id = 0;
			mem->ids[ii].serial_num = 0;
			mem->count --;
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   %u/%u Forgot outstanding request %u:%u (count<-%d)\n",
				       priv->dev->index,priv->id,
				       id->network_id,id->serial_num,mem->count);
			}
#endif

			return 0;
		}
	}
#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Could not forget outstanding request %u:%u (count<-%d)\n",
		       priv->dev->index,priv->id,
		       id->network_id,id->serial_num,mem->count);
	}
#endif
	return -1;
}

/* A message is a reply iff 'in_reply_to' is non-zero */
static int kbus_message_is_reply(struct kbus_message_header *msg)
{
	return !kbus_same_message_id(&msg->in_reply_to,0,0);
}

/*
 * Build a KBUS synthetic message/exception. We assume no data.
 *
 * The message built is a 'pointy' message.
 *
 * - 'name' shall be a constant, or at least shall not be freed during the life
 *   of this synthetic message, since we do not take a copy of it.
 */
static struct kbus_message_header
	*kbus_build_kbus_message(char			*name,
				 uint32_t		 from,
				 uint32_t		 to,
				 struct kbus_msg_id	 in_reply_to)
{
	struct kbus_message_header	*new_msg;

	new_msg = kmalloc(sizeof(*new_msg), GFP_KERNEL);
	if (!new_msg) {
		printk(KERN_ERR "kbus: Cannot kmalloc synthetic message\n");
	       	return NULL;
	}

	memset(new_msg, 0, sizeof(*new_msg));
	new_msg->start_guard = KBUS_MSG_START_GUARD;
	new_msg->from = from;
	new_msg->to = to;
	new_msg->in_reply_to = in_reply_to;
	new_msg->flags = KBUS_BIT_SYNTHETIC;
	new_msg->name_len = strlen(name);
	new_msg->name = name;
	new_msg->end_guard = KBUS_MSG_END_GUARD;

	/*
	 * Remember that message id 0 is reserved for messages from KBUS - we
	 * don't distinguish them (XXX perhaps reconsider this later on XXX)
	 */

	return new_msg;
}

/*
 * Given a message name, is it valid?
 *
 * We have nothing to say on maximum length.
 *
 * Returns 0 if it's OK, 1 if it's naughty
 */
static int kbus_bad_message_name(char *name, size_t name_len)
{
	size_t	 ii;
	int	 dot_at = 1;

	if (name_len < 3) return 1;

	if (name == NULL || name[0] != '$' || name[1] != '.')
		return 1;

	if (name[name_len-2] == '.' && name[name_len-1] == '*')
		name_len -= 2;
	else if (name[name_len-2] == '.' && name[name_len-1] == '%')
		name_len -= 2;
	
	if (name[name_len-1] == '.') return 1;

	for (ii=2; ii<name_len; ii++) {
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
 * Check if a message header is well structured.
 *
 * Return 0 if a message is well-formed, negative otherwise.
 */
static int kbus_check_message(struct kbus_message_header	*msg)
{
	if (msg == NULL) {
		printk(KERN_ERR "kbus: Tried to check NULL message\n");
		return -EINVAL;
	}

	/*
	 * Check the guards match, and that lengths are "plausible"
	 */
	if (msg->start_guard != KBUS_MSG_START_GUARD) {
		printk(KERN_ERR "kbus: message start guard is %08x,"
		       " not %08x\n", msg->start_guard, KBUS_MSG_START_GUARD);
		return -EINVAL;
	}
	if (msg->end_guard != KBUS_MSG_END_GUARD) {
		printk(KERN_ERR "kbus: message end guard is %08x,"
		       " not %08x\n", msg->end_guard, KBUS_MSG_END_GUARD);
		return -EINVAL;
	}

	if (msg->name_len == 0) {
		printk(KERN_ERR "kbus: message name length is 0\n");
		return -EINVAL;
	}
	if (msg->name_len > KBUS_MAX_NAME_LEN) {
		printk(KERN_ERR "kbus: message name length is %u,"
		       " more than %u\n", msg->name_len, KBUS_MAX_NAME_LEN);
		return -ENAMETOOLONG;
	}

	if (msg->name == NULL) {
		uint32_t entire_size = KBUS_ENTIRE_MSG_LEN(msg->name_len,
							   msg->data_len);
		/* Inline message name and (if necessary) data */
		if (msg->data != NULL) {
			printk(KERN_ERR "kbus: Message name is inline, data is not\n");
			return -EINVAL;
		}
		if (entire_size > KBUS_MAX_ENTIRE_LEN) {
			printk(KERN_ERR "kbus: message length is %u,"
			       " more than %u\n", entire_size, KBUS_MAX_ENTIRE_LEN);
			return -EMSGSIZE;
		}
	} else {
		if (msg->data == NULL && msg->data_len != 0) {
			printk(KERN_ERR "kbus: Message data is inline, name is not\n");
			return -EINVAL;
		}
	}

	if (msg->data_len == 0 && msg->data != NULL) {
		printk(KERN_ERR "kbus: Message data length is 0, but data pointer is set\n");
		return -EINVAL;
	}

	if (msg->name == NULL) {
		struct kbus_entire_message	*entire;
		uint32_t	*end_guard_ptr;

		entire = (struct kbus_entire_message *)msg;
		end_guard_ptr = kbus_end_ptr(entire);

#if VERBOSE_DEBUG
		if (*end_guard_ptr != KBUS_MSG_END_GUARD) {
			printk(KERN_DEBUG "kbus: (entire) message end guard is %08x,"
			       " not %08x\n", *end_guard_ptr,
			       KBUS_MSG_END_GUARD);

			{
				uint8_t *cptr = (uint8_t *)entire;
				int i;

				printk(KERN_DEBUG "kbus: Raw msg dump ..\n");
				printk(KERN_DEBUG "kbus: START");
				for (i = 0; i < KBUS_ENTIRE_MSG_LEN(entire->header.data_len, entire->header.name_len); i ++) {
					if (!(i % 8)) {
						printk(KERN_DEBUG "\nkbus: ");
					}
					printk(KERN_DEBUG "%02x ", cptr[i]);

				}
				printk(KERN_DEBUG "\n");
				printk(KERN_DEBUG "kbus: END\n");
			}

			return -EINVAL;
		}
#endif
	}
	return 0;
}

/* This function only defined if VERBOSE_DEBUG is to avoid 
 * 'function defined but not used' warnings from gcc.
 */
#if VERBOSE_DEBUG

/*
 * Output a description of the message
 *
 * Assumes the message is well structured - i.e., that kbus_check_message()
 * has already been used on it.
 */
static void kbus_report_message(char				*kern_prefix,
				struct kbus_message_header	*msg)
{
	char *name_p = NULL;
	void *data_p = NULL;

	if (msg->name == NULL) {
		struct kbus_entire_message	*entire;
		uint32_t			 data_idx;

		entire = (struct kbus_entire_message *)msg;
		name_p = (char *) &entire->rest[0];
		if (msg->data_len) {
			data_idx = KBUS_ENTIRE_MSG_DATA_INDEX(msg->name_len);
			data_p = &entire->rest[data_idx];
		}
	} else {
		name_p = msg->name;
		data_p = msg->data;
	}
	if (msg->data_len) {
		uint32_t	*udata_p = (uint32_t *)data_p;
		printk("%skbus:   =%s= %u:%u '%.*s'"
		       " to %u from %u flags %08x data/%u %08x\n",
		       kern_prefix,
		       msg->name==NULL?"E":"=",
		       msg->id.network_id,msg->id.serial_num,
		       msg->name_len,name_p,
		       msg->to,msg->from,msg->flags,
		       msg->data_len,udata_p[0]);
	} else {
		printk("%skbus:   =%s= %u:%u '%.*s'"
		       " to %u from %u flags %08x\n",
		       kern_prefix,
		       msg->name==NULL?"E":"=",
		       msg->id.network_id,msg->id.serial_num,
		       msg->name_len,name_p,
		       msg->to,msg->from,msg->flags);
	}
}

#endif

/*
 * Copy a message, doing whatever is deemed necessary.
 *
 * Copies the message header, and also copies the message name and any
 * data. The message must be a 'pointy' message with reference counted
 * data.
 */
static struct kbus_message_header *
	kbus_copy_message(struct kbus_private_data	*priv,
			  struct kbus_message_header	*old_msg)
{
	struct kbus_message_header  *new_msg;

	new_msg = kmalloc(sizeof(*new_msg), GFP_KERNEL);
	if (!new_msg) {
		printk(KERN_ERR "kbus: Cannot kmalloc copy of message header\n");
	       	return NULL;
	}
	if (!memcpy(new_msg, old_msg, sizeof(*new_msg))) {
		printk(KERN_ERR "kbus: Cannot copy message header\n");
		kfree(new_msg);
		return NULL;
	}

	/* In case of error before we're finished... */
	new_msg->name = NULL;
	new_msg->data = NULL;

	new_msg->name = kmalloc(new_msg->name_len + 1, GFP_KERNEL);
	if (!new_msg->name) {
		printk(KERN_ERR "kbus: Cannot kmalloc copy of message name\n");
		kfree(new_msg);
	       	return NULL;
	}
	strncpy(new_msg->name, old_msg->name, new_msg->name_len);
	new_msg->name[new_msg->name_len] = 0;

	if (new_msg->data_len) {
		/* Take a new reference to the data */
		new_msg->data = kbus_ref_data(priv,old_msg->data);
	}
	return new_msg;
}

/*
 * Free a message.
 * 
 * If 'deep', then also free the message name and (dereference) any message
 * data.
 */
static void kbus_free_message(struct kbus_private_data	 *priv,
			      struct kbus_message_header *msg,
			      int			  deep)
{
	if (deep) {
		if (msg->name)
			kfree(msg->name);
		msg->name_len = 0;
		msg->name= NULL;

		if (msg->data_len && msg->data) {
			kbus_deref_data(priv,msg->data);
		}
		msg->data_len = 0;
		msg->data= NULL;
	}
	kfree(msg);
}

/* Make space to store the message being read by the user */
static int kbus_alloc_read_msg(struct kbus_private_data *priv,
			       int			 num_parts)
{
	int ii;

	priv->read.parts = kmalloc(sizeof(*(priv->read.parts))*num_parts,
				  GFP_KERNEL);
	if (!priv->read.parts) {
		return -ENOMEM;
	}
	priv->read.lengths = kmalloc(sizeof(*(priv->read.lengths))*num_parts,
				 GFP_KERNEL);
	if (!priv->read.lengths) {
		kfree(priv->read.parts);
		priv->read.parts = NULL;
		return -ENOMEM;
	}
	for (ii=0; ii<num_parts; ii++) {
		priv->read.parts[ii] = NULL;
		priv->read.lengths[ii] = 0;
	}
	priv->read.num_parts = num_parts;
	return 0;
}

static int kbus_realloc_read_msg(struct kbus_private_data *priv,
				 int			   new_num_parts)
{
	int ii;

	priv->read.parts = krealloc(priv->read.parts,
				    sizeof(*(priv->read.parts))*new_num_parts,
				    GFP_KERNEL);
	if (!priv->read.parts) {
		priv->read.parts = NULL;
		return -ENOMEM;
	}
	priv->read.lengths = krealloc(priv->read.lengths,
				      sizeof(*(priv->read.lengths))*new_num_parts,
				      GFP_KERNEL);
	if (!priv->read.lengths) {
		kfree(priv->read.parts);
		priv->read.parts = NULL;
		priv->read.lengths = NULL;
		return -ENOMEM;
	}
	for (ii=priv->read.num_parts; ii<new_num_parts; ii++) {
		priv->read.parts[ii] = NULL;
		priv->read.lengths[ii] = 0;
	}
	priv->read.num_parts = new_num_parts;
	return 0;
}

static void kbus_free_read_msg(struct kbus_private_data *priv)
{
	if (priv->read.parts) {
		kfree(priv->read.parts);
		priv->read.parts = NULL;
	}
	if (priv->read.lengths) {
		kfree(priv->read.lengths);
		priv->read.lengths = NULL;
	}
}

static void kbus_empty_read_msg(struct kbus_private_data *priv)
{
	int  ii;

	if (priv->read.hdr) {
		kfree(priv->read.hdr);
		priv->read.hdr = NULL;
	}

	if (priv->read.parts[KBUS_PART_NAME])
		kfree(priv->read.parts[KBUS_PART_NAME]);

	/* Take care to only "free" any data via its reference count */
	if (priv->read.data) {
		kbus_deref_data(priv, priv->read.data);
		priv->read.data = NULL;
	}

	for (ii=0; ii<priv->read.num_parts; ii++) {
		priv->read.parts[ii] = NULL;
		priv->read.lengths[ii] = 0;
	}

	priv->read.which = 0;
	priv->read.pos = 0;
	return;
}

static void kbus_empty_write_msg(struct kbus_private_data *priv)
{
	if (priv->write_msg)
		kfree(priv->write_msg);
	priv->write_msg = NULL;
	priv->write_msg_size = 0;
	priv->write_msg_len  = 0;
	return;
}

/*
 * Copy the given message, and add it to the end of the queue.
 *
 * This is the *only* way of adding a message to a queue. It shall remain so.
 *
 * We assume the message has been checked for sanity.
 *
 * 'for_replier' is true if this particular message is being pushed to the
 * message's replier's queue. Specifically, it's true if this is a Reply
 * to this KSock, or a Request aimed at this KSock (as Replier).
 *
 * Returns 0 if all goes well, or -EFAULT if we can't allocate datastructures.
 */
static int kbus_push_message(struct kbus_private_data	  *priv,
			     struct kbus_message_header   *msg,
			     int			   for_replier)
{
	struct list_head		*queue = &priv->message_queue;
	struct kbus_message_header	*new_msg = NULL;
	struct kbus_message_queue_item	*item;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Pushing message onto queue (%s)\n",
		       priv->dev->index,priv->id,
		       for_replier?"replier":"listener");
	}
#endif

	/* Check it makes some degreee of sense */
	/*
	 * XXX Is this unnecessary paranoia? If we're being called
	 * XXX (ultimately) from kbus_send, then surely we already
	 * XXX did these checks there. And otherwise we're being
	 * XXX called for a synthetic message, which (a) should be
	 * XXX correct (!), and (b) could check itself before calling
	 * XXX us, if we want that check.
	 */
	if (kbus_check_message(msg)) return -EINVAL;
	if (kbus_bad_message_name(msg->name,msg->name_len)) {
		printk(KERN_ERR "kbus: message name '%.*s' is not allowed\n",
		       msg->name_len,msg->name);
		return -EBADMSG;
	}


	// --------------------------------------------------------------------
	// XXX NEW CODE
	/*
	 * 1. Check to see if this KSock has the "only one copy
	 *    of a message" flag set.
	 * 2. If it does, check if our message (id) is already on
	 *    the queue, and if it is, just skip adding it.
	 *
	 * (this means if the KSock was destined to get the message
	 * several times, either as Replier and Listener, or as
	 * multiple Listeners to the same message name, it will only
	 * get it once, for this "push")
	 */
	// We *always* just push the message if "for_replier" is set
	if (priv->messages_only_once && !for_replier) {
		/*
		 * 1. We've been asked to only send one copy of a message
		 *    to each KSock that should receive it.
		 * 2. This is not a Reply (to our KSock) or a Request (to
		 *    our KSock as Replier)
		 *
		 * So, given that, has a message with that id already been
		 * added to the message queue?
		 */
		if (kbus_same_message_id(&priv->msg_id_just_pushed,
					 msg->id.network_id,
					 msg->id.serial_num)) {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   %u/%u Ignoring message under 'once only' rule\n",
				       priv->dev->index,priv->id);
			}
#endif
			return 0;
		}
	}
	// --------------------------------------------------------------------


	new_msg = kbus_copy_message(priv,msg);
	if (!new_msg) return -EFAULT;

	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		printk(KERN_ERR "kbus: Cannot kmalloc new message item\n");
		kbus_free_message(priv,new_msg,true);
	       	return -ENOMEM;
	}

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		kbus_report_message(KERN_DEBUG,new_msg);
	}
#endif

	if (for_replier && (KBUS_BIT_WANT_A_REPLY & msg->flags)) {
		/*
		 * This message wants a reply, and is for the message's
		 * replier, so they need to be told that they are to reply to
		 * this message
		 */
		new_msg->flags |= KBUS_BIT_WANT_YOU_TO_REPLY;
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Setting WANT_YOU_TO_REPLY, flags %08x\n",
			       new_msg->flags);
		}
#endif

	} else {
		/*
		 * The recipient is *not* the replier for this message,
		 * so it is not responsible for replying.
		 */
		new_msg->flags &= ~KBUS_BIT_WANT_YOU_TO_REPLY;
	}

	/* And join it up... */
	item->msg = new_msg;

	/* By default, we're using the list as a FIFO, so we want to add our
	 * new message to the end (just before the first item). However, if the
	 * URGENT flag is set, then we instead want to add it to the start.
	 */
	if (msg->flags & KBUS_BIT_URGENT) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Message is URGENT\n");
		}
#endif

		list_add(&item->list, queue);
	} else {
		list_add_tail(&item->list, queue);
	}

	priv->message_count ++;
	priv->msg_id_just_pushed = msg->id;

	if (!kbus_same_message_id(&msg->in_reply_to,0,0)) {
		/*
		 * If it's a reply (and this will include a synthetic reply,
		 * since we're checking the "in_reply_to" field) then the
		 * original sender has now had its request satisfied.
		 */
		int retval = kbus_forget_msg_id(priv, &msg->in_reply_to);

		if (retval) {
		  printk(KERN_ERR "kbus: %u/%u Error forgetting outstanding request %u:%u\n",
			 priv->dev->index,priv->id,
			 msg->in_reply_to.network_id,msg->in_reply_to.serial_num);
		  /* But there's not much we can do about it */
		}
	}

	/* And indicate that there is something available to read */
	wake_up_interruptible(&priv->read_wait);

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Leaving %d message%s in queue\n",
		       priv->dev->index, priv->id,
		       priv->message_count,
		       priv->message_count==1?"":"s");
	}
#endif

	return 0;
}

/*
 * Generate a synthetic message, and add it to the recipient's message queue.
 *
 * This is expected to be used when a Reply is not going to be generated
 * by the intended Replier. Since we don't want KBUS itself to block on
 * (trying to) SEND a message to someone not expecting it, I don't think
 * there are any other occasions when it is useful.
 *
 * 'from' is the id of the recipient who has gone away, not received the
 * message, or whatever.
 *
 * 'to' is the 'from' for the message we're bouncing (or whatever).
 *
 * 'in_reply_to' should be the message id of that same message.
 *
 * Doesn't return anything since I can't think of anything useful to do if it
 * goes wrong.
 */
static void kbus_push_synthetic_message(struct kbus_dev		  *dev,
					uint32_t		   from,
					uint32_t		   to,
					struct kbus_msg_id	   in_reply_to,
					char			  *name)
{
	struct kbus_private_data	*priv = NULL;
	struct kbus_message_header	*new_msg;

	/* Who *was* the original message to? */
	priv = kbus_find_open_ksock(dev,to);
	if (!priv) {
		printk(KERN_ERR "kbus: %u Cannot send synthetic reply to %u,"
		       " as they are gone\n",dev->index,to);
		return;
	}

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u Pushing synthetic message '%s'"
		       " onto queue for %u\n",dev->index,name,to);
	}
#endif

	/*
	 * Note that we do not check if the destination queue is full
	 * - we're going to trust that the "keep enough room in the
	 * message queue for a reply to each request" mechanism does
	 * it's job properly.
	 *
	 * XXX Think on this a little harder...
	 * XXX ...although I think it *is* OK
	 */

	new_msg = kbus_build_kbus_message(name, from, to, in_reply_to);
	if (!new_msg) return;

	(void) kbus_push_message(priv, new_msg, false);

	/*
	 * kbus_push_message takes a copy of our message data, but
	 * we don't want to free the message name!
	 */
	kbus_free_message(priv, new_msg, false);

	return;
}

/*
 * Generate a bind/unbind synthetic message, and broadcast it.
 *
 * This is for use when we have been asked to announce when a Replier binds or
 * unbinds.
 *
 * 'is_bind' is true if this was a "bind" event, false if it was an "unbind".
 *
 * 'name' is the message name (or wildcard) that was bound (or unbound) to.
 *
 * Doesn't return anything since I can't think of anything useful to do if it
 * goes wrong.
 */
static void kbus_push_synthetic_bind_message(struct kbus_dev	  *dev,
					     uint32_t		   from,
					     uint32_t		   is_bind,
					     uint32_t		   name_len,
					     char		  *name)
{
	struct kbus_private_data	*priv = NULL;
	struct kbus_message_header	*new_msg;
	struct kbus_msg_id		 in_reply_to = {0,0};	/* no-one */

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u Pushing synthetic message '%s'"
		       " onto queue\n",dev->index,name);
	}
#endif

	/*
	 * XXX THIS IS NOT SUFFICIENT FOR WHAT THIS ROUTINE IS TO DO
	 * XXX ...RETHINK...
	 *
	 * Note that we do not check if the destination queue is full
	 * - we're going to trust that the "keep enough room in the
	 * message queue for a reply to each request" mechanism does
	 * it's job properly.
	 */

	new_msg = kbus_build_kbus_message("$.KBUS.Replier.BindEvent",
					  from, 0, in_reply_to);
	if (!new_msg) return;

	/* XXX And its data... XXX */

	/* XXX CHECK CHECK CHECK XXX */
	(void) kbus_push_message(priv, new_msg, false);

	/*
	 * kbus_push_message takes a copy of our message data, but
	 * we don't want to free the message name!
	 */
	kbus_free_message(priv, new_msg, false);

	return;
}

/*
 * Pop the next message off our queue.
 *
 * Returns a pointer to the message, or NULL if there is no next message.
 */
static struct kbus_message_header *kbus_pop_message(struct kbus_private_data *priv)
{
	struct list_head		*queue = &priv->message_queue;
	struct kbus_message_queue_item	*item;
	struct kbus_message_header	*msg = NULL;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Popping message from queue\n",
		       priv->dev->index,priv->id);
	}
#endif

	if (list_empty(queue))
		return NULL;

	/* Retrieve the next message */
	item = list_first_entry(queue, struct kbus_message_queue_item, list);

	/* And straightway remove it from the list */
	list_del(&item->list);

	priv->message_count --;

	msg = item->msg;
	kfree(item);

	/* If doing that made us go from no-room to some-room, wake up */
	if (priv->message_count == (priv->max_messages - 1))
		wake_up_interruptible(&priv->dev->write_wait);

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		kbus_report_message(KERN_DEBUG, msg);

		printk(KERN_DEBUG "kbus:   %u/%u Leaving %d message%s in queue\n",
		       priv->dev->index, priv->id,
		       priv->message_count,
		       priv->message_count==1?"":"s");
	}
#endif

	return msg;
}

/*
 * Empty a message queue. Send synthetic messages for any outstanding
 * request messages that are now not going to be delivered/replied to.
 */
static int kbus_empty_message_queue(struct kbus_private_data  *priv)
{
	struct list_head		*queue = &priv->message_queue;
	struct kbus_message_queue_item	*ptr;
	struct kbus_message_queue_item	*next;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Emptying message queue\n",
		       priv->dev->index,priv->id);
	}
#endif

	list_for_each_entry_safe(ptr, next, queue, list) {
		struct kbus_message_header	*msg = ptr->msg;
		int  is_OUR_request = (KBUS_BIT_WANT_YOU_TO_REPLY & msg->flags);

#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			kbus_report_message(KERN_DEBUG, msg);
		}
#endif

		/*
		 * If it wanted a reply (from us). let the sender know it's
		 * going away (but take care not to send a message to
		 * ourselves, by accident!)
		 */
		if (is_OUR_request && msg->to != priv->id ) {
			kbus_push_synthetic_message(priv->dev,priv->id,
						    msg->from,msg->id,
						    "$.KBUS.Replier.GoneAway");
		}

		/* Remove it from the list */
		list_del(&ptr->list);
		/* And forget all about it... */
		kbus_free_message(priv, ptr->msg, true);

		priv->message_count --;
	}
#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Leaving %d message%s in queue\n",
		       priv->dev->index, priv->id,
		       priv->message_count,
		       priv->message_count==1?"":"s");
	}
#endif

	return 0;
}

/*
 * Add a message to the list of messages read by the replier, but still needing
 * a reply.
 */
static int kbus_reply_needed(struct kbus_private_data   *priv,
			     struct kbus_message_header	*msg)
{
	struct list_head		*queue = &priv->replies_unsent;
	struct kbus_unreplied_item	*item;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Adding message %u:%u to unsent replies list\n",
		       priv->dev->index,priv->id,
		       msg->id.network_id,msg->id.serial_num);
	}
#endif


	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		printk(KERN_ERR "kbus: Cannot kmalloc reply-needed item\n");
	       	return -ENOMEM;
	}

	/* For the moment, take a *copy* of the message name */
	item->name = kmalloc(msg->name_len+1, GFP_KERNEL);
	if (!item->name) {
		printk(KERN_ERR "kbus: Cannot kmalloc reply-needed item's name\n");
		kfree(item);
		return -ENOMEM;
	}
	strncpy(item->name, kbus_name_ptr(msg), msg->name_len);
	item->name[msg->name_len] = 0;

	item->id = msg->id;
	item->from = msg->from;
	item->name_len = msg->name_len;

	list_add(&item->list, queue);

	priv->num_replies_unsent ++;

	if (priv->num_replies_unsent > priv->max_replies_unsent)
		priv->max_replies_unsent = priv->num_replies_unsent;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Leaving %d message%s unreplied-to\n",
		       priv->dev->index, priv->id,
		       priv->num_replies_unsent,
		       priv->num_replies_unsent==1?"":"s");
	}
#endif

	return 0;
}

/*
 * Remove a message from the list of (read) messages needing a reply
 *
 * Returns 0 on success, -1 if it could not find the message
 */
static int kbus_reply_now_sent(struct kbus_private_data  *priv,
			       struct kbus_msg_id	 *msg_id)
{
	struct list_head		*queue = &priv->replies_unsent;
	struct kbus_unreplied_item	*ptr;
	struct kbus_unreplied_item	*next;

	list_for_each_entry_safe(ptr, next, queue, list) {
		if (kbus_same_message_id(&ptr->id,
					 msg_id->network_id, msg_id->serial_num)) {

#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   %u/%u Reply to %u:%u %*s now sent\n",
			       priv->dev->index,priv->id,
			       msg_id->network_id, msg_id->serial_num,
			       ptr->name_len, ptr->name);
		}
#endif

			/* Remove it from the list */
			list_del(&ptr->list);
			/* And forget all about it... */
			kfree(ptr->name);
			kfree(ptr);

			priv->num_replies_unsent --;

#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   %u/%u Leaving %d message%s unreplied-to\n",
			       priv->dev->index, priv->id,
			       priv->num_replies_unsent,
			       priv->num_replies_unsent==1?"":"s");
		}
#endif

			return 0;
		}
	}

	printk(KERN_ERR "kbus: %u/%u Could not find message %u:%u in unsent replies list\n",
	       priv->dev->index,priv->id,
	       msg_id->network_id,msg_id->serial_num);
	return -1;
}

/*
 * Empty our "replies unsent" queue. Send synthetic messages for any
 * request messages that are now not going to be replied to.
 */
static int kbus_empty_replies_unsent(struct kbus_private_data  *priv)
{
	struct list_head		*queue = &priv->replies_unsent;
	struct kbus_unreplied_item	*ptr;
	struct kbus_unreplied_item	*next;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Emptying unreplied messages list\n",
		       priv->dev->index,priv->id);
	}
#endif

	list_for_each_entry_safe(ptr, next, queue, list) {

		kbus_push_synthetic_message(priv->dev,priv->id,
					    ptr->from,ptr->id,
					    "$.KBUS.Replier.Ignored");

		/* Remove it from the list */
		list_del(&ptr->list);
		/* And forget all about it... */
		kfree(ptr->name);
		kfree(ptr);

		priv->num_replies_unsent --;
	}
#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Leaving %d message%s unreplied-to\n",
		       priv->dev->index, priv->id,
		       priv->num_replies_unsent,
		       priv->num_replies_unsent==1?"":"s");
	}
#endif

	return 0;
}

/*
 * Find out who, if anyone, is bound as a replier to the given message name.
 *
 * Returns 1 if we found a replier, 0 if we did not (but all went well), and
 * a negative value if something went wrong.
 */
static int kbus_find_replier(struct kbus_dev		 *dev,
			     struct kbus_private_data	**bound_to,
			     uint32_t			  name_len,
			     char			 *name)
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
		if ( ptr->is_replier &&
		     ptr->name_len == name_len &&
		     !strncmp(name,ptr->name,name_len) ) {
#if VERBOSE_DEBUG
			if (dev->verbose) {
				printk(KERN_DEBUG "kbus:   %u '%.*s' has replier %u\n",
				       dev->index,
				       ptr->name_len,ptr->name,ptr->bound_to_id);
			}
#endif

			*bound_to = ptr->bound_to;
			return 1;
		}
	}
	return 0;
}

/*
 * Find out who, if anyone, is bound as listener/replier to this message name.
 *
 * 'listeners' is an array of (pointers to) listener bindings. It may be NULL
 * (if there are no listeners or if there was an error). It is up to the caller
 * to free it. It does not include (pointers to) any replier binding.
 *
 * If there is also a replier for this message, then 'replier' will be (a
 * pointer to) its binding, otherwise it will be NULL. The replier will not be
 * in the 'listeners' array, so the caller must check both.
 *
 * Note that a particular listener may be present more than once, if that
 * particular listener has bound to the message more than once (but no
 * *binding* will be represented more than once).
 *
 * Returns the number of listeners found (i.e., the length of the array), and a
 * negative value if something went wrong. This is a bit clumsy, because the
 * caller needs to check the return value *and* the 'replier' value, but there
 * is only one caller, so...
 */
static int kbus_find_listeners(struct kbus_dev			 *dev,
			       struct kbus_message_binding	**listeners[],
			       struct kbus_message_binding	**replier,
			       uint32_t				  name_len,
			       char				 *name)
{
#define INIT_LISTENER_ARRAY_SIZE	8
	int count = 0;
	int array_size = INIT_LISTENER_ARRAY_SIZE;
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

#if VERBOSE_DEBUG
	if (dev->verbose) {
		printk(KERN_DEBUG "kbus:   Looking for listeners/repliers for '%.*s'\n",
		       name_len,name);
	}
#endif


	*listeners = kmalloc(sizeof(struct kbus_message_binding *) * array_size,GFP_KERNEL);
	if (!(*listeners)) return -ENOMEM;

	*replier = NULL;

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {

		if (kbus_message_name_matches(name,name_len,ptr->name)) {
#if VERBOSE_DEBUG
			if (dev->verbose) {
				printk(KERN_DEBUG "kbus:      Name '%.*s' matches '%s' for %s %u\n",
				       name_len, name,
				       ptr->name, ptr->is_replier?"replier":"listener",
				       ptr->bound_to_id);
			}
#endif


			if (ptr->is_replier) {
				/* It *may* be the replier for this message */
				size_t	last_char = strlen(ptr->name) - 1;
				if (ptr->name[last_char] == '*')
					new_replier_type = WILD_STAR;
				else if (ptr->name[last_char] == '%')
					new_replier_type = WILD_PERCENT;
				else
					new_replier_type = SPECIFIC;


#if VERBOSE_DEBUG
				if (dev->verbose && *replier) {
					printk(KERN_DEBUG "kbus:      ..previous replier was %u (%s), looking at %u (%s)\n",
					       ((*replier) == NULL?0:(*replier)->bound_to_id),
					       REPLIER_TYPE(replier_type),
					       ptr->bound_to_id, REPLIER_TYPE(new_replier_type));
				}
#endif

				/*
				 * If this is the first replier, just remember
				 * it. Otherwise, if it's more specific than
				 * our previous replier, remember it instead.
				 */
				if (*replier == NULL ||
				    new_replier_type > replier_type) {
				  
#if VERBOSE_DEBUG
					if (dev->verbose && *replier) {
						printk(KERN_DEBUG "kbus:      ..going with replier %u (%s)\n",
						       ptr->bound_to_id,
						       REPLIER_TYPE(new_replier_type));
					}
#endif

					*replier = ptr;
					replier_type = new_replier_type;
				} else {
#if VERBOSE_DEBUG
					if (dev->verbose && *replier) {
						printk(KERN_DEBUG "kbus:      ..keeping replier %u (%s)\n",
						       (*replier)->bound_to_id,
						       REPLIER_TYPE(replier_type));
					}
#endif
				}
			} else {
				/* It is a listener */
				if (count == array_size) {
					uint32_t new_size = kbus_next_size(array_size);
#if VERBOSE_DEBUG
					if (dev->verbose) {
						printk(KERN_DEBUG "kbus:      XXX listener array size %d -> %d\n",
						       array_size, new_size);
					}
#endif

					array_size = new_size;
					*listeners = krealloc(*listeners,
							      sizeof(**listeners) * array_size,
							      GFP_KERNEL);
					if (!(*listeners))
						return -EFAULT;
				}
				(*listeners)[count++] = ptr;
			}
		}
	}
#if VERBOSE_DEBUG
	if (dev->verbose) {
		printk(KERN_DEBUG "kbus:      Found %d listener%s%s for '%.*s'\n",
		       count, (count==1?"":"s"),
		       (*replier==NULL?"":" and a replier"),
		       name_len,name);
	}
#endif

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
static int kbus_remember_binding(struct kbus_dev	  *dev,
				 struct kbus_private_data *priv,
				 uint32_t		   replier,
				 uint32_t		   name_len,
				 char			  *name)
{
	int retval = 0;
	struct kbus_message_binding *new;

	/* If we want a replier, and there already is one, we lose */
	if (replier) {
		struct kbus_private_data *reply_to;
		retval = kbus_find_replier(dev, &reply_to, name_len, name);
		/*
		 * "Address in use" isn't quite right, but let's the caller
		 * have some hope of telling what went wrong, and this is a
		 * useful case to distinguish.
		 */
		if (retval == 1) return -EADDRINUSE;
	}

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new) return -ENOMEM;

	new->bound_to = priv;
	new->bound_to_id = priv->id;	/* Useful shorthand? */
	new->is_replier = replier;
	new->name_len = name_len;
	new->name = name;

	list_add(&new->list, &dev->bound_message_list);
	return 0;
}

/*
 * Decide if we should keep this message, because we are still bound to it.
 */
static int kbus_we_should_keep_this_message(struct kbus_private_data    *priv,
					    int				 is_our_request,
					    uint32_t			 name_len,
					    char			*name)
{
	struct kbus_message_binding	**listeners = NULL;
	struct kbus_message_binding	 *replier = NULL;
	int		 num_listeners, ii;
	int		 still_wanted = false;

	/*
	 * This is rather a heavy hammer to wield, but we shouldn't do it
	 * *that* often, and it does guarantee to do (what we think is) the
	 * Right Thing.
	 */
	num_listeners = kbus_find_listeners(priv->dev,&listeners,&replier,
					    name_len,name);

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:    ___ is our request %d, replier %u, num_listeners %d\n",
		       is_our_request,(replier?replier->bound_to_id:0),num_listeners);
	}
#endif


	if (num_listeners < 0)		/* Hmm, what else to do on error? */
		return true;
	else if (num_listeners == 0)
		goto done;

	/*
	 * If the message wanted us, specifically, to reply to it, are we
	 * still bound to do so?
	 */
	if (is_our_request) {
	       if (replier && replier->bound_to_id == priv->id) {
#if VERBOSE_DEBUG
		       if (priv->dev->verbose) {
			       printk(KERN_DEBUG "kbus:    ___ we are replier\n");
		       }
#endif

		       still_wanted = true;
		       goto done;
	       } else {
#if VERBOSE_DEBUG
		       if (priv->dev->verbose) {
			       printk(KERN_DEBUG "kbus:    ___ we are not replier\n");
		       }
#endif

		       still_wanted = false;
		       goto done;
	       }
	}

	for (ii=0; ii<num_listeners; ii++) {
		if (listeners[ii]->bound_to_id == priv->id) {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:    ___ we are listener\n");
			}
#endif

			still_wanted = true;
			goto done;
		}
	}
done:
	kfree(listeners);
	return still_wanted;
}

/*
 * Forget any messages (in our queue) that were only in the queue because of
 * the binding we've just removed.
 *
 * If the message was a request (needing a reply) generate an appropriate
 * synthetic message.
 */
static int kbus_forget_matching_messages(struct kbus_private_data  *priv,
					 uint32_t		    bound_to_id,
					 uint32_t		    bound_as_replier,
					 uint32_t		    name_len,
					 char			   *name)
{
	struct list_head		*queue = &priv->message_queue;
	struct kbus_message_queue_item	*ptr;
	struct kbus_message_queue_item	*next;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Forgetting matching messages\n",
		       priv->dev->index,priv->id);
	}
#endif


	list_for_each_entry_safe(ptr, next, queue, list) {
		struct kbus_message_header	*msg = ptr->msg;
		int  is_a_request = (KBUS_BIT_WANT_A_REPLY & msg->flags);
		int  is_OUR_request = (KBUS_BIT_WANT_YOU_TO_REPLY & msg->flags);

		/*
		 * Deciding on the basis of message name is easy, so do that
		 * first
		 */
		if (msg->name_len != name_len)
			continue;
		if (strncmp(msg->name,name,name_len))
			continue;

#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   >>> bound_as_replier %d, is_OUR_request %d\n",
			       bound_as_replier,is_OUR_request);
			kbus_report_message(KERN_DEBUG, msg);
		}
#endif

		/*
		 * If this message didn't require a reply, and the binding
		 * we just removed was a replier binding, then we presumably
		 * haven't affected this message
		 */
		if ( !is_a_request && bound_as_replier ) {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   >>> removed replier binding, message not our business\n");
			}
#endif

			continue;
		}

		/*
		 * If this message required US to reply, and we've just
		 * removed a listener binding, then we presumably haven't
		 * affected this message.
		 */
		if ( is_OUR_request && !bound_as_replier ) {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   >>> removed listener binding, specific request not our business\n");
			}
#endif

			continue;
		}

		/*
		 * OK, it looks like it matches.
		 *
		 * However, we don't know if there is another binding in the
		 * bindings list that matches as well (even for a replier, we
		 * might be bound with a wildcard and with a more specific
		 * binding)
		 */
		if (kbus_we_should_keep_this_message(priv,
						     is_OUR_request,
						     msg->name_len,
						     msg->name))
			continue;

#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Deleting message from queue\n");
			kbus_report_message(KERN_DEBUG, msg);
		}
#endif

		/*
		 * If it wanted a reply (from us). let the sender know it's
		 * going away (but take care not to send a message to
		 * ourselves, by accident!)
		 */
		if (is_OUR_request && msg->to != priv->id ) {
			kbus_push_synthetic_message(priv->dev,priv->id,
						    msg->from,msg->id,
						    "$.KBUS.Replier.Unbound");
		}

		/* Remove it from the list */
		list_del(&ptr->list);
		/* And forget all about it... */
		kbus_free_message(priv, ptr->msg, true);

		priv->message_count --;

		/* If doing that made us go from no-room to some-room, wake up */
		if (priv->message_count == (priv->max_messages - 1))
			wake_up_interruptible(&priv->dev->write_wait);
	}
#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Leaving %d message%s in queue\n",
		       priv->dev->index, priv->id,
		       priv->message_count, priv->message_count==1?"":"s");
	}
#endif

	return 0;
}

/*
 * Remove an existing binding.
 *
 * Returns 0 if all went well, a negative value if it did not.
 */
static int kbus_forget_binding(struct kbus_dev		*dev,
			       struct kbus_private_data *priv,
			       uint32_t			 bound_to_id,
			       uint32_t			 replier,
			       uint32_t			 name_len,
			       char			*name)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	int removed_binding = false;

	/* We don't want anyone writing to the list whilst we do this */
	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		if (bound_to_id != ptr->bound_to_id)
			continue;
		if (replier != ptr->is_replier)
			continue;
		if (name_len != ptr->name_len)
			continue;
		if ( !strncmp(name,ptr->name,name_len) ) {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   %u/%u Unbound %u %c '%.*s'\n",
				       dev->index,priv->id,
				       ptr->bound_to_id,
				       (ptr->is_replier?'R':'L'),
				       ptr->name_len,
				       ptr->name);
			}
#endif

			/* And we don't want anyone reading for this */
			list_del(&ptr->list);
			if (ptr->name)
				kfree(ptr->name);
			kfree(ptr);

			/*
			 * Leave dealing with the implications of this until
			 * we're safely out of our iteration over this list.
			 * This means the code to do such dealing doesn't need
			 * to care if we're already manipulating this list
			 * (just in case).
			 */
			removed_binding = true;
			break;
		}
	}

	if (removed_binding) {
		/* And forget any messages we now shouldn't receive */
		(void) kbus_forget_matching_messages(priv,bound_to_id,
						     replier,name_len,name);
		return 0;
	} else {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   %u/%u Could not find/unbind %u %c '%.*s'\n",
			       dev->index,priv->id,
			       bound_to_id,
			       (replier?'R':'L'),
			       name_len,name);
		}
#endif

		return -EINVAL;
	}

	/*
	 * We carefully don't try to do anything about requests that have
	 * already been read - the fact that the user has unbound from
	 * receiving new messages with this name doesn't imply anything about
	 * whether they're going to reply to requests (with that name) which
	 * they've already read.
	 */
}

/*
 * Remove all bindings for a particular listener.
 *
 * Called from kbus_release, which will itself handle removing messages
 * (that *were* bound) from the message queue.
 */
static void kbus_forget_my_bindings(struct kbus_dev	*dev,
				   uint32_t		 bound_to_id)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		if (bound_to_id == ptr->bound_to_id) {
#if VERBOSE_DEBUG
			if (dev->verbose) {
				printk(KERN_DEBUG "kbus:   Unbound %u %c '%.*s'\n",
				       ptr->bound_to_id,
				       (ptr->is_replier?'R':'L'),
				       ptr->name_len,
				       ptr->name);
			}
#endif

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
 * Assumed to be called because the device is closing, and thus doesn't lock,
 * nor does it worry about generating synthetic messages as requests are doomed
 * not to get replies.
 */
static void kbus_forget_all_bindings(struct kbus_dev	*dev)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
#if VERBOSE_DEBUG
		if (dev->verbose) {
			printk(KERN_DEBUG "kbus:   Unbinding %u %c '%.*s'\n",
			       ptr->bound_to_id,
			       (ptr->is_replier?'R':'L'),
			       ptr->name_len,
			       ptr->name);
		}
#endif

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
static int kbus_remember_open_ksock(struct kbus_dev		*dev,
				    struct kbus_private_data	*priv)
{
	list_add(&priv->list, &dev->open_ksock_list);

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: Remembered 'open file' id %u\n",priv->id);
	}
#endif

	return 0;
}

/*
 * Retrieve the pointer to an open file's data
 *
 * Return NULL if we can't find it.
 */
static struct kbus_private_data *kbus_find_open_ksock(struct kbus_dev	*dev,
						      uint32_t		 id)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	list_for_each_entry_safe(ptr, next, &dev->open_ksock_list, list) {
		if (id == ptr->id) {
#if VERBOSE_DEBUG
			if (dev->verbose) {
				printk(KERN_DEBUG "kbus:   %u Found open KSock %u\n",
				       dev->index,id);
			}
#endif
			return ptr;
		}
	}
#if VERBOSE_DEBUG
	if (dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u Could not find open KSock %u\n",dev->index,id);
	}
#endif

	return NULL;
}

/*
 * Remove an open file remembrance.
 *
 * Returns 0 if all went well, a negative value if it did not.
 */
static int kbus_forget_open_ksock(struct kbus_dev	*dev,
				  uint32_t		 id)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	/* We don't want anyone writing to the list whilst we do this */
	list_for_each_entry_safe(ptr, next, &dev->open_ksock_list, list) {
		if (id == ptr->id) {
#if VERBOSE_DEBUG
			if (dev->verbose) {
				printk(KERN_DEBUG "kbus:   %u Forgetting open Ksock %u\n",
				       dev->index,id);
			}
#endif

			/* So remove it from our list */
			list_del(&ptr->list);
			/* But *we* mustn't free the actual datastructure! */
			return 0;
		}
	}
#if VERBOSE_DEBUG
	if (dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u Could not forget open KSock %u\n",
		       dev->index,id);
	}
#endif

	return -EINVAL;
}

/*
 * Forget all our "open file" remembrances.
 *
 * Assumed to be called because the device is closing, and thus doesn't lock.
 */
static void kbus_forget_all_open_ksocks(struct kbus_dev	*dev)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	list_for_each_entry_safe(ptr, next, &dev->open_ksock_list, list) {
#if VERBOSE_DEBUG
		if (dev->verbose) {
			printk(KERN_DEBUG "kbus:   %u Forgetting open KSock %u\n",
			       dev->index,ptr->id);
		}
#endif

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
	if (!priv) return -ENOMEM;

	/*
	 * Use the official magic to retrieve our actual device data
	 * so we can remember it for other file operations.
	 */
	dev = container_of(inode->i_cdev, struct kbus_dev, cdev);

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

	memset(priv, 0, sizeof(*priv));
	priv->dev = dev;
	priv->id  = dev->next_file_id ++;
	priv->pid = current->pid;
	priv->max_messages = DEF_MAX_MESSAGES;
	priv->sending = false;
	priv->num_replies_unsent = 0;
	priv->max_replies_unsent = 0;

	if (kbus_alloc_read_msg(priv, KBUS_MIN_NUM_PARTS))
	{
		kfree(priv);
		return -ENOMEM;
	}

	if (kbus_init_msg_id_memory(priv)) {
		kbus_free_read_msg(priv);
		kfree(priv);
		return -EFAULT;
	}
	INIT_LIST_HEAD(&priv->message_queue);
	INIT_LIST_HEAD(&priv->replies_unsent);

	init_waitqueue_head(&priv->read_wait);

	/* Note that we immediately have a space available for a message */
	wake_up_interruptible(&dev->write_wait);

	(void) kbus_remember_open_ksock(dev,priv);

	filp->private_data = priv;

	up(&dev->sem);

#if VERBOSE_DEBUG
	/* Can't conditionalise this on priv->dev->verbose, because we've only just
	 * opened it
	 */
	printk(KERN_DEBUG "kbus: %u/%u OPEN\n",dev->index,priv->id);
#endif


	return 0;
}

static int kbus_release(struct inode *inode, struct file *filp)
{
	int	retval1 = 0;
	int	retval2 = 0;
	int	retval3 = 0;
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

#if VERBOSE_DEBUG
	/* If we always show "open", we should match it here */
	printk(KERN_DEBUG "kbus: %u/%u RELEASE\n",dev->index,priv->id);
#endif

	kbus_empty_read_msg(priv);
	kbus_free_read_msg(priv);
	kbus_empty_write_msg(priv);

	kbus_empty_msg_id_memory(priv);

	retval1 = kbus_empty_message_queue(priv);
	kbus_forget_my_bindings(dev,priv->id);
	retval2 = kbus_forget_open_ksock(dev,priv->id);
	retval3 = kbus_empty_replies_unsent(priv);
	kfree(priv);

	up(&dev->sem);

	if (retval1)
		return retval1;
	else if (retval2)
		return retval2;
	else
		return retval3;
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
#if VERBOSE_DEBUG
		if (dev->verbose) {
			printk(KERN_DEBUG "kbus:   -- Id %u is us\n",id);
		}
#endif

		l_priv = our_priv;
	} else {
		/* OK, look it up */
#if VERBOSE_DEBUG
		if (dev->verbose) {
			printk(KERN_DEBUG "kbus:   -- Looking up id %u\n",id);
		}
#endif

		l_priv = kbus_find_open_ksock(dev,id);
	}
	return l_priv;
}

/*
 * Determine if the specified recipient has room for a message in their queue
 *
 * - 'priv' is the recipient
 * - 'what' is a string describing them (e.g., "sender", "replier"), just
 *   for use in debugging/grumbling
 * - if 'is_reply' is true, then we're checking for a Reply message,
 *   which we already know is expected by the specified recipient.
 */
static int kbus_queue_is_full(struct kbus_private_data	*priv,
			      char			*what,
			      int			 is_reply)
{
	/*
	 * When figuring out how "full" the message queue is, we need
	 * to take account of the messages already in the queue (!),
	 * and also the replies that still need to be written to the
	 * queue.
	 *
	 * Of course, if we're checking because we want to send one
	 * of the Replies who we are keeping room for, we need to
	 * remember to account for that!
	 */
	int already_accounted_for = priv->message_count +
				    priv->outstanding_requests.count;

	if (is_reply)
		already_accounted_for --;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   %u/%u Message queue: count %d + outstanding %d %s= %d, max %d\n",
		       priv->dev->index, priv->id,
		       priv->message_count, priv->outstanding_requests.count,
		       (is_reply?"-1 ":""), already_accounted_for, priv->max_messages);
	}
#endif

	if (already_accounted_for < priv->max_messages) {
		return false;
	} else {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Message queue for %s %u is full"
			       " (%u+%u%s > %u messages)\n",what,priv->id,
			       priv->message_count,
			       priv->outstanding_requests.count,
			       (is_reply?"-1":""),
			       priv->max_messages);
		}
#endif

		return true;
	}
}

/*
 * Actually write to anyone interested in this message.
 *
 * Remember that the caller is going to free the message data after
 * calling us, on the assumption that we're taking a copy...
 *
 * Returns 0 on success.
 *
 * If the message is a Request, and there is no replier for it, then we return
 * -EADDRNOTAVAIL.
 *
 * If the message couldn't be sent because some of the targets (those that we
 * *have* to deliver to) had full queues, then it will return -EAGAIN or
 * -EBUSY. If -EAGAIN is returned, then the caller should try again later, if
 * -EBUSY then it should not.
 *
 * Otherwise, it returns a negative value for error.
 */
static int32_t kbus_write_to_recipients(struct kbus_private_data   *priv,
					struct kbus_dev		   *dev,
					struct kbus_message_header *msg)
{
	struct kbus_message_binding	**listeners = NULL;
	struct kbus_message_binding	 *replier = NULL;
	struct kbus_private_data	 *reply_to = NULL;
	ssize_t		 retval = 0;
	int		 num_listeners;
	int		 ii;
	int		 num_sent = 0;	/* # successfully "sent" */

	int	all_or_fail = msg->flags & KBUS_BIT_ALL_OR_FAIL;
	int	all_or_wait = msg->flags & KBUS_BIT_ALL_OR_WAIT;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   all_or_fail %d, all_or_wait %d\n",
		       all_or_fail,all_or_wait);
	}
#endif


	/*
	 * Remember that
	 * (a) a listener may occur more than once in our array, and
	 * (b) we have 0 or 1 repliers, but
	 * (c) the replier is *not* one of the listeners.
	 */
	num_listeners = kbus_find_listeners(dev,&listeners,&replier,
					    msg->name_len,msg->name);
	if (num_listeners < 0) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Error %d finding listeners\n",
			       num_listeners);
		}
#endif

		retval = num_listeners;
		goto done_sending;
	}

	/*
	 * In general, we don't mind if no-one is listening, but
	 *
	 * a. If we want a reply, we want there to be a replier
	 * b. If we *are* a reply, we want there to be an original sender
	 * c. If we have the "to" field set, and we want a reply, then we
	 *    want that specific replier to exist
	 *
	 * We can check the first of those immediately.
	 */

	if (msg->flags & KBUS_BIT_WANT_A_REPLY && replier == NULL) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Message wants a reply, but no replier\n");
		}
#endif

		retval = -EADDRNOTAVAIL;
		goto done_sending;
	}

	/* And we need to add it to the queue for each interested party */

	/*
	 * ===================================================================
	 * Check if the proposed recipients *can* receive
	 * ===================================================================
	 */

	/*
	 * Are we replying to a sender's request?
	 * Replies are unusual in that the recipient will not normally have
	 * bound to the appropriate message name.
	 */
	if (kbus_message_is_reply(msg)) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Considering sender-of-request %u\n",
			       msg->to);
		}
#endif


		reply_to = kbus_find_private_data(priv,dev,msg->to);
		if (reply_to == NULL) {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   Can't find sender-of-request %u\n",msg->to);
			}
#endif

			/* Which really means something nasty has gone wrong */
			retval = -EFAULT;	/* XXX What *should* we say? */
			goto done_sending;
		}

		/* Are they expecting this reply? */
		if (kbus_find_msg_id(reply_to, &msg->in_reply_to)) {
			/* No, so we aren't allowed to send it */
			retval = -ECONNREFUSED;
			goto done_sending;
		}

		if (kbus_queue_is_full(reply_to,"sender-of-request",true)) {
			if (all_or_wait)
				retval = -EAGAIN;	/* try again later */
			else
				retval = -EBUSY;
			goto done_sending;
		}
	}

	/* Repliers only get request messages */
	if (replier && !(msg->flags & KBUS_BIT_WANT_A_REPLY))
		replier = NULL;

	/*
	 * And even then, only if they have room in their queue
	 * Note that it is *always* fatal (to this send) if we can't
	 * add a Request to a Replier's queue -- we just need to figure
	 * out what sort of error to return
	 */
	if (replier) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Considering replier %u\n",
			       replier->bound_to_id);
		}
#endif

		/*
		 * If the 'to' field was set, then we only want to send it if
		 * it is *that* specific replier (and otherwise we want to fail
		 * with "that's the wrong person for this (stateful) request").
		 */
		if (msg->to && (replier->bound_to_id != msg->to)) {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   ..Request to %u,"
				       " but replier is %u\n",msg->to,replier->bound_to_id);
			}
#endif

			retval = -EPIPE;	/* Well, sort of */
			goto done_sending;
		}

		if (kbus_queue_is_full(replier->bound_to,"replier",false)) {
			if (all_or_wait)
				retval = -EAGAIN;	/* try again later */
			else
				retval = -EBUSY;
			goto done_sending;
		}
	}

	for (ii=0; ii<num_listeners; ii++) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Considering listener %u\n",
			       listeners[ii]->bound_to_id);
		}
#endif


		if (kbus_queue_is_full(listeners[ii]->bound_to,"listener",false)) {
			if (all_or_wait) {
				retval = -EAGAIN;	/* try again later */
				goto done_sending;
			} else if (all_or_fail) {
				retval = -EBUSY;
				goto done_sending;
			} else {
				/* For now, just ignore *this* listener */
				listeners[ii] = NULL;
				continue;
			}
		}
	}

	/*
	 * ===================================================================
	 * Actually send the messages
	 * ===================================================================
	 */

	/*
	 * Remember that kbus_push_message takes a copy of the message for us.
	 *
	 * This is inefficient, since otherwise we could keep a single copy of
	 * the message (or at least the message header) and just bump a
	 * reference count for each "use" of the message name/data.
	 *
	 * However, it also allows us to easily set the "needs a reply" flag
	 * (and associated data) when sending a "needs a reply" message to a
	 * replier, and *unset* the same when sending said message to "just"
	 * listeners...
	 *
	 * Be careful if altering this...
	 */

	/*
	 * We know that kbus_push_message() can return 0 or -EFAULT.
	 * It seems sensible to treat that latter as a "local" error, as it
	 * means that our internals have gone wrong. Thus we don't need to
	 * generate a message for it.
	 */

	/* If it's a reply message and we've got someone to reply to, send it */
	if (reply_to) {
		retval = kbus_push_message(reply_to,msg,true);
		if (retval == 0) {
			num_sent ++;
			/*
			 * In which case, we *have* sent this reply,
			 * and can forget about needing to do so
			 * (there's not much we can do with an error
			 * in this, so just ignore it)
			 */
			(void) kbus_reply_now_sent(priv,&msg->in_reply_to);
		} else {
			goto done_sending;
		}
	}

	/* If it's a request, and we've got a replier for it, send it */
	if (replier) {
		retval = kbus_push_message(replier->bound_to,msg,true);
		if (retval == 0) {
			num_sent ++;
			/* And we'll need a reply for that, thank you */
			retval = kbus_remember_msg_id(priv, &msg->id);
			if (retval) {
				/*
				 * Out of memory - what *can* we do?
				 * (basically, nothing, it's all gone horribly
				 * wrong)
				 */
				goto done_sending;
			}
		} else {
			goto done_sending;
		}
	}

	/* For each listener, if they're still interested, send it */
	for (ii=0; ii<num_listeners; ii++) {
		struct kbus_message_binding *listener = listeners[ii];
		if (listener) {
			retval = kbus_push_message(listener->bound_to,msg,false);
			if (retval == 0)
				num_sent ++;
			else
				goto done_sending;
		}
	}

	retval = 0;

done_sending:
	if (listeners)
		kfree(listeners);
	return retval;
}

static ssize_t kbus_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *f_pos)
{
	struct kbus_private_data	*priv = filp->private_data;
	struct kbus_dev			*dev = priv->dev;
	ssize_t				 retval = 0;


	if (down_interruptible(&dev->sem))
		return -EAGAIN;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u WRITE count %d, pos %d\n",
		       dev->index,priv->id,count,(int)*f_pos);
	}
#endif

	/*
	 * If we've already started to try sending a message, we don't
	 * want to continue appending to it
	 */
	if (priv->sending) {
		retval = -EALREADY;
		goto done;
	}

	/*
	 * When the user is writing a message to us, we insist that it
	 * not be "too long". If the user wants to write a large amount
	 * of data in the message, then they should use the "pointy" form,
	 * which won't trigger this...
	 */
	if ( (count + priv->write_msg_len) > KBUS_MAX_ENTIRE_LEN) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_ERR "kbus: Message appears to be too long (max %d)\n",
			       KBUS_MAX_ENTIRE_LEN);
		}
#endif

		kbus_empty_write_msg(priv);
		retval = -EMSGSIZE;
		goto done;
	}

	if (priv->write_msg == NULL) {
		size_t start_size = KBUS_ENTIRE_MSG_LEN(16,0);	/* As a random guess */
		priv->write_msg_size = max(start_size,count);
		priv->write_msg = kmalloc(priv->write_msg_size, GFP_KERNEL);
		if (!priv->write_msg) {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_ERR "kbus/write: kmalloc failed\n");
			}
#endif

			retval = -ENOMEM;
			goto done;
		}
		priv->write_msg_len = 0;
	} else if ( (count + priv->write_msg_len) > priv->write_msg_size ) {
		/*
		 * Just go for the size actually needed, in the hope that
		 * this is the last write?
		 */
		priv->write_msg_size = count + priv->write_msg_len;
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   XXX Reallocating message buffer as %d bytes\n",
			       priv->write_msg_size);
		}
#endif

		priv->write_msg = krealloc(priv->write_msg,
					   priv->write_msg_size, GFP_KERNEL);
		if (!priv->write_msg) {
			printk(KERN_ERR "kbus/write: krealloc failed\n");
		       	retval = -EFAULT;
			goto done;
		}
	}

	if (copy_from_user(priv->write_msg + priv->write_msg_len,
			   buf, count)) {
		printk(KERN_ERR "kbus: copy from user failed (%d to %p + %u)\n",
		       count,priv->write_msg,priv->write_msg_len);
		kbus_empty_write_msg(priv);
		retval = -EFAULT;
		goto done;
	}
	priv->write_msg_len += count;

done:
	up(&dev->sem);
	if (retval == 0)
		return count;
	else
		return retval;
}

static ssize_t kbus_read(struct file *filp, char __user *buf, size_t count,
			 loff_t *f_pos)
{
	struct kbus_private_data	*priv = filp->private_data;
	struct kbus_dev			*dev = priv->dev;
	ssize_t				 retval = 0;
	uint32_t			 len, left;
	uint32_t			 which = priv->read.which;

	if (down_interruptible(&dev->sem))
		return -EAGAIN;			/* Just try again later */

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u READ count %d, pos %d\n",
		       dev->index,priv->id,count,(int)*f_pos);
	}
#endif

	if (priv->read.hdr == NULL) {
		/* No message to read at the moment */
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Nothing to read\n");
		}
#endif

		retval = 0;
		goto done;
	}

	if (which == KBUS_PART_HDR && priv->read.pos == 0) {
		/*
		 * We're just starting our write. Make sure that when
		 * we write the message header out, it doesn't look like
		 * a "pointy" header. Note that after we do this, it's
		 * technically broken, since (as it stands) it's not a
		 * legitimate "entire" header, either...
		 */
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   xx kbus_read ========================\n");
			printk(KERN_DEBUG "kbus:   xx Unsetting name and header pointers\n");
		}
#endif

		priv->read.hdr->name = NULL;
		priv->read.hdr->data = NULL;
	}

	/*
	 * Read each of the parts of a message (header, name, padding, data,
	 * padding, final end guard), until we're read 'count' characters, or
	 * run off the end of the message.
	 */
	while (which < priv->read.num_parts && count > 0) {
		if (priv->read.lengths[which]) {
			left = priv->read.lengths[which] - priv->read.pos;
			len = min(left,count);

#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   xx which %d, read_len[%d] %u, pos %u,"
				       " left %u, len %u, count %u\n",
				       which,which,priv->read.lengths[which],priv->read.pos,left,len,
				       count);
			}
#endif


			if (len) {
				if (copy_to_user(buf,
						 priv->read.parts[which] + priv->read.pos,
						 len)) {
					printk(KERN_ERR "kbus: error reading from dev %u/%u\n",
					       dev->index,priv->id);
					retval = -EFAULT;
					goto done;
				}
				buf += len;
				retval += len;
				count  -= len;
				priv->read.pos += len;
#if 0
				printk(KERN_DEBUG "kbus:      so far %d, count %u, pos %u\n",
				       retval, count, priv->read.pos);
#endif
			}

			if (priv->read.pos == priv->read.lengths[which]) {
				priv->read.pos = 0;
				which ++;
			}
		} else {
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   xx which %d, read_len[%d] %u\n",
				       which,which,priv->read.lengths[which]);
			}
#endif

			priv->read.pos = 0;
			which ++;
		}
	}

	if (which < priv->read.num_parts) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Read %d bytes, now in part %d, read %d of %d\n",
			       retval,which,priv->read.pos,priv->read.lengths[which]);
		}
#endif

		priv->read.which = which;
	} else {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Read %d bytes, finished\n",retval);
		}
#endif

		kbus_empty_read_msg(priv);
	}

done:
	up(&dev->sem);
	return retval;
}

static int kbus_bind(struct kbus_private_data	*priv,
		     struct kbus_dev		*dev,
		     unsigned long		 arg)
{
	int		retval = 0;
	struct kbus_bind_request *bind;
	char		*name = NULL;

	bind = kmalloc(sizeof(*bind), GFP_KERNEL);
	if (!bind) return -ENOMEM;
	if (copy_from_user(bind, (void *)arg, sizeof(*bind))) {
		retval = -EFAULT;
		goto done;
	}

	if (bind->name_len == 0) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: bind name is length 0\n");
		}
#endif

		retval = -EBADMSG;
		goto done;
	} else if (bind->name_len > KBUS_MAX_NAME_LEN) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: bind name is length %d\n",
			       bind->name_len);
		}
#endif

		retval = -ENAMETOOLONG;
		goto done;
	}

	name = kmalloc(bind->name_len+1, GFP_KERNEL);
	if (!name) {
		retval = -ENOMEM;
		goto done;
	}
	if (copy_from_user(name, bind->name, bind->name_len)) {
		retval = -EFAULT;
		goto done;
	}
	name[bind->name_len] = 0;


	if (kbus_bad_message_name(name,bind->name_len)) {
		retval = -EBADMSG;
		goto done;
	}

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u BIND %c '%.*s'\n",
		       priv->dev->index,priv->id,
		       (bind->is_replier?'R':'L'), bind->name_len, name);
	}
#endif

	retval = kbus_remember_binding(dev, priv,
				       bind->is_replier,
				       bind->name_len,
				       name);
	if (retval == 0) {
		/* The binding will use our copy of the message name */
		name = NULL;
	}

#if 1 // XXX
	if (retval == 0 &&
	    bind->is_replier &&
	    priv->dev->report_replier_binds) {
		kbus_push_synthetic_bind_message(priv->dev, priv->id, true,
						 bind->name_len, bind->name); /* XXX */
	}
#endif

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
	struct kbus_bind_request *bind;
	char		*name = NULL;

	bind = kmalloc(sizeof(*bind), GFP_KERNEL);
	if (!bind) return -ENOMEM;
	if (copy_from_user(bind, (void *)arg, sizeof(*bind))) {
		retval = -EFAULT;
		goto done;
	}

	if (bind->name_len == 0) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: unbind name is length 0\n");
		}
#endif

		retval = -EBADMSG;
		goto done;
	} else if (bind->name_len > KBUS_MAX_NAME_LEN) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: unbind name is length %d\n",
			       bind->name_len);
		}
#endif

		retval = -ENAMETOOLONG;
		goto done;
	}

	name = kmalloc(bind->name_len+1, GFP_KERNEL);
	if (!name) {
		retval = -ENOMEM;
		goto done;
	}
	if (copy_from_user(name, bind->name, bind->name_len)) {
		retval = -EFAULT;
		goto done;
	}
	name[bind->name_len] = 0;

	if (kbus_bad_message_name(name,bind->name_len)) {
		retval = -EBADMSG;
		goto done;
	}

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u UNBIND %c '%.*s'\n",
		       priv->dev->index,priv->id,
		       (bind->is_replier?'R':'L'), bind->name_len, name);
	}
#endif

	retval = kbus_forget_binding(dev, priv, priv->id,
				     bind->is_replier,
				     bind->name_len,
				     name);

#if 1 // XXX
	if (retval == 0 &&
	    bind->is_replier &&
	    priv->dev->report_replier_binds) {
		kbus_push_synthetic_bind_message(priv->dev, priv->id, false,
						 bind->name_len, bind->name); /* XXX */
	}
#endif

done:
	if (name) kfree(name);
	kfree(bind);
	return retval;
}

static int kbus_replier(struct kbus_private_data	*priv,
			struct kbus_dev			*dev,
			unsigned long			 arg)
{
	struct kbus_private_data	*replier;
	struct kbus_bind_query		*query;
	char 				*name = NULL;
	int				 retval = 0;

	query = kmalloc(sizeof(*query), GFP_KERNEL);
	if (!query) return -ENOMEM;
	if (copy_from_user(query, (void *)arg, sizeof(*query))) {
		retval = -EFAULT;
		goto done;
	}

	if (query->name_len == 0 || query->name_len > KBUS_MAX_NAME_LEN) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: Replier name is length %d\n",query->name_len);
		}
#endif

		retval = -ENAMETOOLONG;
		goto done;
	}

	name = kmalloc(query->name_len+1, GFP_KERNEL);
	if (!name) {
		retval = -ENOMEM;
		goto done;
	}
	if (copy_from_user(name, query->name, query->name_len)) {
		retval = -EFAULT;
		goto done;
	}
	name[query->name_len] = 0;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u REPLIER for '%.*s'\n",
		       priv->dev->index,priv->id, query->name_len, name);
	}
#endif

	retval = kbus_find_replier(dev,
				   &replier,
				   query->name_len,
				   name);
	if (retval < 0) goto done;

	if (retval)
	       	query->return_id = replier->id;
	else
		query->return_id = 0;
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

/*
 * Make the next message ready for reading by the user.
 *
 * Returns 0 if there is no next message, 1 if there is, and a negative value
 * if there's an error.
 */
static int kbus_nextmsg(struct kbus_private_data	*priv,
			struct kbus_dev			*dev,
			unsigned long			 arg)
{
	int	retval = 0;
	int     total_parts;
	int     ii;
	struct kbus_message_header *msg;
	struct kbus_data_ptr       *ref;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u NEXTMSG\n",priv->dev->index,priv->id);
	}
#endif

	/* If we were partway through a message, lose it */
	if (priv->read.hdr) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   Dropping partial message\n");
		}
#endif
		kbus_empty_read_msg(priv);
	}

	/* Have we got a next message? */
	msg = kbus_pop_message(priv);
	if (msg == NULL) {
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus:   No next message\n");
		}
#endif
		/*
		 * A return value of 0 means no message, and that's
		 * what __put_user returns for success.
		 */
		return __put_user(0, (uint32_t __user *)arg);
	}

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   xx Setting up read_hdr\n");
	}
#endif

	ref = msg->data;

	/* How many parts will our output data be in? */
	total_parts = KBUS_MIN_NUM_PARTS;
	if (msg->data_len)
		total_parts += ref->num_parts + 1;

	if (priv->read.parts == NULL) {
		retval = kbus_alloc_read_msg(priv, total_parts);
		if (retval) return retval;
	} else if (priv->read.num_parts < total_parts) {
		retval = kbus_realloc_read_msg(priv, total_parts);
		if (retval) return retval;
	}

	priv->read.hdr = msg;
	priv->read.data = msg->data;

	priv->read.parts[KBUS_PART_HDR] = (char *)msg;
	priv->read.parts[KBUS_PART_NAME] = msg->name;
	priv->read.parts[KBUS_PART_NPAD] = static_zero_padding;
	if (msg->data) {
		for (ii=0; ii<ref->num_parts; ii++)
			priv->read.parts[KBUS_PART_DATA_START+ii] = (void *)ref->parts[ii];
		priv->read.parts[total_parts-2] = static_zero_padding;
	}
	priv->read.parts[total_parts-1] = (char *)&static_end_guard;

	priv->read.lengths[KBUS_PART_HDR]  = sizeof(struct kbus_message_header);
	priv->read.lengths[KBUS_PART_NAME] = msg->name_len;
	priv->read.lengths[KBUS_PART_NPAD] = KBUS_PADDED_NAME_LEN(msg->name_len) - msg->name_len;
	if (msg->data) {
		for (ii=0; ii<ref->num_parts; ii++)
			priv->read.lengths[KBUS_PART_DATA_START+ii] = ref->lengths[ii];
		priv->read.lengths[total_parts-2] = KBUS_PADDED_DATA_LEN(msg->data_len) - msg->data_len;
	}
	priv->read.lengths[total_parts-1]  = 4;

	priv->read.num_parts = total_parts;

	/* And we'll be starting by writing out the first thing first */
	priv->read.which = 0;
	priv->read.pos = 0;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   @@ Next message, %d parts, lengths ",total_parts);
		for (ii=0; ii<total_parts; ii++)
			printk("%u ",priv->read.lengths[ii]);
		printk("\n");
	}
#endif

	/*
	 * If the message is a request (to us), then this is the approriate
	 * point to add it to our list of "requests we've read but not yet
	 * replied to" -- although that *sounds* as if we should be doing it in
	 * kbus_read, we might never get round to reading the content of the
	 * message (we might call NEXTMSG again, or DISCARD), and also
	 * kbus_read can get called multiple times for a single message body.*
	 * If we do our remembering here, then we guarantee to get one memory
	 * for each request, as it leaves the message queue and is (in whatever
	 * way) dealt with.
	 */
	if (msg->flags & KBUS_BIT_WANT_YOU_TO_REPLY) {
		retval = kbus_reply_needed(priv, msg);
		/* If it couldn't malloc, there's not much we can do, it's fairly fatal */
		if (retval) return retval;
	}

	retval = __put_user(KBUS_ENTIRE_MSG_LEN(msg->name_len,msg->data_len),
			    (uint32_t __user *)arg);
	if (retval)
		return retval;
	else
		return 1;	/* We had a message */
}

/* How much of the current message is left to read? */
static uint32_t kbus_lenleft(struct kbus_private_data	*priv)
{
	if (priv->read.hdr) {
		int      ii;
		uint32_t sofar = 0;
		uint32_t total = KBUS_ENTIRE_MSG_LEN(priv->read.hdr->name_len,
						     priv->read.hdr->data_len);
		/* Add up the items we're read all of, so far */
		for (ii=0; ii<priv->read.which; ii++) {
			sofar += priv->read.lengths[ii];
		}
		/* Plus what we're read of the last one */
		if (priv->read.which < priv->read.num_parts)
			sofar += priv->read.pos;
		return total - sofar;
	} else {
		return 0;
	}
}

/*
 * Does what it says on the box - take the user data and promote it to kernel
 * space, as a reference counted quantity, possibly spread over multiple pages.
 */
static int kbus_wrap_data(struct kbus_private_data	 *priv,
			  struct kbus_message_header	 *msg,
			  int				  original_is_pointy,
			  void				**new_data)
{
#define PART_LEN	PAGE_SIZE
#define PAGE_THRESHOLD	(PAGE_SIZE >> 1)

	int            num_parts = (msg->data_len + PART_LEN-1)/PART_LEN;

	unsigned long *parts = NULL;
	unsigned      *lengths = NULL;
	int            as_pages;
	int            ii;
	unsigned long  data_ptr;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   @@ part len %lu, threshold %lu, data_len %u -> num_parts %d\n",
		       PART_LEN, PAGE_THRESHOLD, msg->data_len, num_parts);
	}
#endif

	parts = kmalloc(sizeof(*parts)*num_parts, GFP_KERNEL);
	if (!parts)
		return -ENOMEM;
	lengths = kmalloc(sizeof(*lengths)*num_parts, GFP_KERNEL);
	if (!parts) {
		kfree(parts);
		return -ENOMEM;
	}

	if (num_parts == 1 && msg->data_len < PAGE_THRESHOLD) {
		/* A single part in "simple" memory */
		as_pages = false;
		parts[0] = (unsigned long)kmalloc(msg->data_len, GFP_KERNEL);
		if (!parts[0]) {
			kfree(lengths);
			kfree(parts);
			return -ENOMEM;
		}
	} else {
		/*
		 * One or more pages
		 * We (deliberately) do not care if the last page has
		 * much data in it or not...
		 */
		as_pages = true;
		for (ii = 0; ii < num_parts; ii++) {
			parts[ii] = __get_free_page(GFP_KERNEL);
			if (!parts[ii]) {
				int jj;
				for (jj=0; jj<ii; jj++)
					free_page(parts[jj]);
				kfree(lengths);
				kfree(parts);
				return -ENOMEM;
			}
		}
	}
#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   @@ copying %s, original is %s\n",
		       as_pages?"as pages":"with memcpy", original_is_pointy?"pointy":"entire");
	}
#endif

	if (original_is_pointy) {
		data_ptr = (unsigned long) msg->data;
		for (ii=0; ii<num_parts; ii++) {
			unsigned len;
			if (ii == num_parts-1)
				len = msg->data_len - (num_parts-1)*PART_LEN;
			else
				len = PART_LEN;
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   @@ %d: copy %d bytes from user address %lu\n",
				       ii, len, parts[ii]);
			}
#endif

			if (copy_from_user((void *)parts[ii],
					   (void *)data_ptr, len)) {
				int jj;
				if (as_pages)
					for (jj=0; jj<num_parts; jj++)
						free_page(parts[jj]);
				else
					kfree((void *)parts[0]);
				kfree(lengths);
				kfree(parts);
				return -EFAULT;
			}
			lengths[ii] = len;
			data_ptr += PART_LEN;
		}
	} else {
		/*
		 * For an "entire" message, the user is not allowed
		 * a very large amount of data - definitely less than
		 * a page - so this can be simple.
		 */
		data_ptr = (unsigned long) kbus_data_ptr(msg);
		for (ii=0; ii<num_parts; ii++) {
			unsigned len;
			if (ii == num_parts-1)
				len = msg->data_len - (num_parts-1)*PART_LEN;
			else
				len = PART_LEN;
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus:   @@ %d: copy %d bytes from kernel address %lu\n",
				       ii, len, parts[ii]);
			}
#endif

			memcpy((void *)parts[ii], (void *)data_ptr, len);
			lengths[ii] = len;
			data_ptr += PART_LEN;
		}
	}
	/* Wrap the result up in a reference */
	*new_data = kbus_new_data_ref(priv,as_pages,num_parts,parts,lengths);
	if (!*new_data) {
		int jj;
		if (as_pages)
			for (jj=0; jj<num_parts; jj++)
				free_page(parts[jj]);
		else
			kfree((void *)parts[0]);
		kfree(lengths);
		kfree(parts);
		return -EFAULT;
	}
	return 0;
}

/*
 * Make an incoming message "pointy", and in kernel space.
 *
 * If there is data, this *always* copies it, even if it was already in kernel
 * space (because of being part of an "entire" message). This is guaranteed to
 * be inefficient for long messages. So try not to do that.
 *
 * Note that after this, the internal message header uses a reference to the
 * data, rather than a simple pointer.
 *
 * Also checks the legality of the message name, since we need the name in
 * kernel space to do that, but prefer to do the check before copying any
 * data (which can be expensive).
 */
static int kbus_make_message_pointy(struct kbus_private_data	  *priv,
				    struct kbus_message_header   **original)
{
	struct kbus_message_header *msg = *original;
	char  *new_name = NULL;
	void  *new_data = NULL;
	int    original_is_pointy;

	/* First, let's deal with the name */
	if (msg->name == NULL) {
		new_name = kmalloc(msg->name_len + 1, GFP_KERNEL);
		if (!new_name) {
			return -ENOMEM;
		}
		strncpy(new_name, kbus_name_ptr(msg), msg->name_len);
		new_name[msg->name_len] = 0;
		original_is_pointy = false;
	} else {
		new_name = kmalloc(msg->name_len + 1, GFP_KERNEL);
		if (!new_name) {
			return -ENOMEM;
		}
		if (copy_from_user(new_name, (void *)msg->name, msg->name_len+1)) {
			kfree(new_name);
			return -EFAULT;
		}
		original_is_pointy = true;
	}

	/*
	 * We can check the name now it is in kernel space - we want
	 * to do this before we sort out the data, since that can involve
	 * a *lot* of copying...
	 */
	if (kbus_bad_message_name(new_name,msg->name_len)) {
		printk(KERN_ERR "kbus: message name '%.*s' is not allowed\n",
		       msg->name_len,new_name);
		kfree(new_name);
		return -EBADMSG;
	}

	/* We don't allow sending to wildcards */
	if (new_name[msg->name_len-1] == '*' ||
	    new_name[msg->name_len-1] == '%') {
		printk(KERN_ERR "kbus: sending to wildcards not allowed, message name '%.*s'\n",
		       msg->name_len,new_name);
		kfree(new_name);
		return -EBADMSG;
	}

	/*
	 * Now for the data.
	 *
	 * NB: We know that if the message name pointer is NULL, then the
	 *     data pointer must also be NULL (kbus_check_message() will have
	 *     checked for this)
	 */

	if (msg->data_len) {
		int retval = kbus_wrap_data(priv, msg, original_is_pointy, &new_data);
		if (retval) {
			kfree(new_name);
			return retval;
		}
	}

	/*
	 * And if we had an "entire" message from the user, let's
	 * make the header the (new) correct size.
	 */
	if (!original_is_pointy) {
		msg = krealloc(msg, sizeof(struct kbus_message_header), GFP_KERNEL);
		if (!msg) {
			kbus_deref_data(priv,new_data);
			kfree(new_name);
			return -EFAULT;
		}
	}

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus:   '%s' message normalised\n",
		       original_is_pointy?"pointy":"entire");
	}
#endif

	msg->name = new_name;
	msg->data = new_data;

	*original = msg;
	return 0;
}

/*
 * Return: negative for bad message, etc., 0 for "general success",
 * and 1 for "we happen to know it got added to the target KSock queues"
 */
static int kbus_send(struct kbus_private_data	*priv,
		     struct kbus_dev		*dev,
		     unsigned long		 arg)
{
	ssize_t		 retval = 0;
	int		 pointers_are_local = false;

	struct kbus_message_header *msg = NULL; /* NULL to make the compiler happy */

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u SEND\n",priv->dev->index,priv->id);
	}
#endif

	if (priv->write_msg == NULL)
		return -ENOMSG;

	/*
	 * We must have at least a minimum message length
	 */
	if (priv->write_msg_len < sizeof(struct kbus_message_header))
	{
		printk(KERN_ERR "kbus: message header too short, %d instead of %d\n",
		       priv->write_msg_len, sizeof(struct kbus_message_header));
		retval = -EINVAL;	/* XXX Consider if there's better */
		goto done;
	}

	/*
	 * It might be a message header or an entire message, but this
	 * should suffice for either
	 */
	msg = (struct kbus_message_header *)priv->write_msg;

	/* Check the message makes sense */
	retval = kbus_check_message(msg);
	if (retval) goto done;

	/* It's not legal to set both ALL_OR_WAIT and ALL_OR_FAIL */
	if ((msg->flags & KBUS_BIT_ALL_OR_WAIT) &&
	    (msg->flags & KBUS_BIT_ALL_OR_FAIL)) {
		printk(KERN_ERR "kbus: Message cannot have both ALL_OR_WAIT and ALL_OR_FAIL set\n");
		retval = -EINVAL;
		goto done;
	}

	/*
	 * Users are not allowed to send messages marked as "synthetic"
	 * (since, after all, if the user sends it, it is not). However,
	 * it's possible that, in good faith, they re-sent a synthetic
	 * message that they received earlier, so we shall take care to
	 * unset the bit, if necessary.
	 */
	if ( KBUS_BIT_SYNTHETIC & msg->flags ) {
		msg->flags &= ~KBUS_BIT_SYNTHETIC;
	}

	/*
	 * The message header is already in kernel space (thanks to kbus_write),
	 * but any name and data may not be, and the data definitely won't be
	 * in the right "shape". So let's fix that.
	 *
	 * Note that we *always* end up with a message header containing
	 * pointers to (copies of) the name and (if given) data, and the
	 * data reference counted, and maybe split over multiple pages.
	 *
	 * (For convenience, this process also checks the message name
	 * is plausible, before it copies any data)
	 */
        retval = kbus_make_message_pointy(priv, &msg);
	if (retval) {
		goto done;
	} else {
		pointers_are_local = true;
	}

	/* ================================================================= */
	/*
	 * If this message is a Request, then we can't send it until/unless
	 * we've got room in our message queue to receive the Reply.
	 *
	 * We do this check here, rather than in kbus_write_to_recipients,
	 * because:
	 *
	 * a) kbus_write_to_recipients gets (re)called by the POLL interface,
	 *    and at that stage KBUS *knows* that there is room for the
	 *    message concerned (so the checking code would need to know not
	 *    to check)
	 *
	 * b) If the check fails, we do not want to consider ourselves in
	 *    "sending" state, since we can't afford to block, because it's
	 *    *this KSock* that needs to do some reading to clear the relevant
	 *    queue, and it can't do that if it's blocking. So we'd either
	 *    need to handle that (somehow), or just do the check here.
	 *
	 * Similarly, we don't finalise the message (put in its "from" and "id"
	 * fields) until we pass this test.
	 */
	if ((msg->flags & KBUS_BIT_WANT_A_REPLY) &&
	    kbus_queue_is_full(priv,"sender",false)) {
		printk(KERN_ERR "kbus:   Unable to send Request because no"
		       " room in sender's message queue\n");
		retval = -ENOLCK;
		goto done;
	}
	/* ================================================================= */

	/* So, we're actually ready to SEND! */

	/* The message needs to say it is from us */
	msg->from = priv->id;

	/*
	 * If we've already tried to send this message earlier (and
	 * presumably failed with -EAGAIN), then we don't need to give
	 * it a message id, because it already has one...
	 */
	if (!priv->sending) {
		/* The message seems well formed, give it an id if necessary */
		if (msg->id.network_id == 0) {
			if (dev->next_msg_serial_num == 0)
				dev->next_msg_serial_num ++;
			msg->id.serial_num = dev->next_msg_serial_num ++;
		}
	}

	/* Also, remember this as the "message we last (tried to) send" */
	priv->last_msg_id_sent = msg->id;

	/*
	 * Figure out who should receive this message, and write it to them
	 */
	retval = kbus_write_to_recipients(priv, dev, msg);

done:
	/*
	 * -EAGAIN means we were blocked from sending, and the caller
	 *  should try again (as one might expect).
	 */
	if (retval == -EAGAIN) {
		/* Remember we're still trying to send this message */
		priv->sending = true;
	} else {
		/* We've now finished with our copy of the message header */
		kbus_discard(priv);

		/*
		 * If we got far enough to copy the name/data, don't forget
		 * to free our copies (since the underlying kbus_push_message()
		 * will have taken copies)
		 */
		if (pointers_are_local) {
			kfree(msg->name);
			kbus_deref_data(priv,msg->data);
		}
	}

	if (retval == 0 || retval == -EAGAIN) {
		if (copy_to_user((void *)arg, &priv->last_msg_id_sent,
				 sizeof(priv->last_msg_id_sent)))
			retval = -EFAULT;
	}
	return retval;
}

static void kbus_discard(struct kbus_private_data	*priv)
{
	kbus_empty_write_msg(priv);
	priv->sending = false;
}

static int kbus_maxmsgs(struct kbus_private_data	*priv,
			struct kbus_dev			*dev,
			unsigned long			 arg)
{
	int		retval = 0;
	uint32_t	requested_max;

	retval = __get_user(requested_max, (uint32_t __user *)arg);
	if (retval) return retval;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u MAXMSGS requests %u (was %u)\n",
		       priv->dev->index,priv->id,
		       requested_max, priv->max_messages);
	}
#endif

	/* A value of 0 is just a query for what the current length is */
	if (requested_max > 0)
		priv->max_messages = requested_max;

	return __put_user(priv->max_messages, (uint32_t __user *)arg);
}

static int kbus_onlyonce(struct kbus_private_data	*priv,
			 struct kbus_dev		*dev,
			 unsigned long			 arg)
{
	int		retval = 0;
	uint32_t	only_once;
	int		old_value = priv->messages_only_once;

	retval = __get_user(only_once, (uint32_t __user *)arg);
	if (retval) return retval;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u ONLYONCE requests %u (was %d)\n",
		       priv->dev->index,priv->id, only_once, old_value);
	}
#endif

	switch (only_once) {
	case 0:
		priv->messages_only_once = false;
		break;
	case 1:
		priv->messages_only_once = true;
		break;
	case 0xFFFFFFFF:
		break;
	default:
		return -EINVAL;
	}

	return __put_user(old_value, (uint32_t __user *)arg);
}

static int kbus_set_verbosity(struct kbus_private_data	*priv,
			      struct kbus_dev		*dev,
			      unsigned long		 arg)
{
	int		retval = 0;
	uint32_t	verbose;
	int		old_value = priv->dev->verbose;

	retval = __get_user(verbose, (uint32_t __user *)arg);
	if (retval) return retval;

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u VERBOSE requests %u (was %d)\n",
		       priv->dev->index,priv->id, verbose, old_value);
	}
#endif

	switch (verbose) {
	case 0:
		priv->dev->verbose = false;
		break;
	case 1:
		priv->dev->verbose = true;
		break;
	case 0xFFFFFFFF:
		break;
	default:
		return -EINVAL;
	}

	return __put_user(old_value, (uint32_t __user *)arg);
}

static int kbus_set_report_binds(struct kbus_private_data	*priv,
				 struct kbus_dev		*dev,
				 unsigned long			 arg)
{
	int		retval = 0;
	uint32_t	report_replier_binds;
	int		old_value = priv->dev->report_replier_binds;

	retval = __get_user(report_replier_binds, (uint32_t __user *)arg);
	if (retval) return retval;

#if VERBOSE_DEBUG
	if (priv->dev->report_replier_binds) {
		printk(KERN_DEBUG "kbus: %u/%u REPORTREPLIERBINDS requests %u (was %d)\n",
		       priv->dev->index,priv->id, report_replier_binds, old_value);
	}
#endif

	switch (report_replier_binds) {
	case 0:
		priv->dev->report_replier_binds = false;
		break;
	case 1:
		priv->dev->report_replier_binds = true;
		break;
	case 0xFFFFFFFF:
		break;
	default:
		return -EINVAL;
	}

	return __put_user(old_value, (uint32_t __user *)arg);
}

static int kbus_ioctl(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	int err = 0;
	int retval = 0;
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;
	uint32_t	 id  = priv->id;

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
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: %u/%u RESET\n",dev->index,id);
		}
#endif
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

	case KBUS_IOC_KSOCKID:
		/*
		 * What is the "KSock id" for this file descriptor
		 */
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: %u/%u KSOCKID %u\n",dev->index,id,id);
		}
#endif
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
			uint32_t left = kbus_lenleft(priv);
#if VERBOSE_DEBUG
			if (priv->dev->verbose) {
				printk(KERN_DEBUG "kbus: %u/%u LENLEFT %u\n",
				       dev->index,id,left);
			}
#endif

			retval = __put_user(left, (uint32_t __user *)arg);
		}
		break;

	case KBUS_IOC_SEND:
		/*
		 * Send the curent message, we've finished writing it.
		 *
		 * arg in: <ignored>
		 * arg out: the message id of said message, and an additional
		 *          return value, containing:
		 *
		 *          * 0 for we know nothing extra
		 *          * 1 for we know it got sent (i.e., we know it got
		 *            added to the target message queues)
		 *          * 2 for we know it didn't get sent (i.e., we know
		 *            something went wrong, probably adding it to the
		 *            target message queues). There will be an error
		 *            message waiting to be read.
		 *          * <other values to be determined>
		 * retval: negative for bad message, etc., 0 otherwise
		 */
		retval = kbus_send(priv,dev, arg);
		break;

	case KBUS_IOC_DISCARD:
		/* Throw away the message we're currently writing. */
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: %u/%u DISCARD\n",dev->index,id);
		}
#endif
		kbus_discard(priv);
		break;

	case KBUS_IOC_LASTSENT:
		/*
		 * What was the message id of the last message written to this
		 * file descriptor? Before any messages have been written to this
		 * file descriptor, this ioctl will return {0,0).
		 */
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: %u/%u LASTSENT %u:%u\n",dev->index,id,
			       priv->last_msg_id_sent.network_id,
			       priv->last_msg_id_sent.serial_num);
		}
#endif
		if (copy_to_user((void *)arg, &priv->last_msg_id_sent,
				      sizeof(priv->last_msg_id_sent)))
			retval = -EFAULT;
		break;

	case KBUS_IOC_MAXMSGS:
		/*
		 * Set (and/or query) maximum number of messages in this
		 * interfaces queue.
		 *
		 * arg in: 0 (for query) or maximum number wanted
		 * arg out: maximum number allowed
		 * return: 0 means OK, otherwise not OK
		 */
		retval = kbus_maxmsgs(priv, dev, arg);
		break;

	case KBUS_IOC_NUMMSGS:
		/* How many messages are in our queue? */
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: %u/%u NUMMSGS %d\n",
			       dev->index,id,priv->message_count);
		}
#endif
		retval = __put_user(priv->message_count, (uint32_t __user *)arg);
		break;

	case KBUS_IOC_UNREPLIEDTO:
		/* How many Requests (to us) do we still owe Replies to? */
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: %u/%u UNREPLIEDTO %d\n",
			       dev->index,id,priv->num_replies_unsent);
		}
#endif
		retval = __put_user(priv->num_replies_unsent, (uint32_t __user *)arg);
		break;

	case KBUS_IOC_MSGONLYONCE:
		/*
		 * Should we receive a given message only once?
		 *
		 * arg in: 0 (for no), 1 (for yes), 0xFFFFFFFF (for query)
		 * arg out: the previous value, before we were called
		 * return: 0 means OK, otherwise not OK
		 */
		retval = kbus_onlyonce(priv, dev, arg);
		break;

	case KBUS_IOC_VERBOSE:
		/*
		 * Should we printk verbose messages?
		 *
		 * arg in: 0 (for no), 1 (for yes), 0xFFFFFFFF (for query)
		 * arg out: the previous value, before we were called
		 * return: 0 means OK, otherwise not OK
		 */
		retval = kbus_set_verbosity(priv, dev, arg);
		break;

	case KBUS_IOC_NEWDEVICE:
		/*
		 * Request a new device
		 *
		 * arg out: the new device number
		 * return: 0 means OK, otherwise not OK.
		 */
#if VERBOSE_DEBUG
		if (priv->dev->verbose) {
			printk(KERN_DEBUG "kbus: %u/%u NEWDEVICE %d\n",
			       dev->index,id,kbus_num_devices);
		}
#endif
		retval = kbus_setup_new_device(kbus_num_devices);
		if (retval > 0) {
			kbus_num_devices++;
			retval = __put_user(kbus_num_devices-1, (uint32_t __user *)arg);
		}
		break;

	case KBUS_IOC_REPORTREPLIERBINDS:
		/*
		 * Should we report Replier bind/unbind events?
		 *
		 * arg in: 0 (for no), 1 (for yes), 0xFFFFFFFF (for query)
		 * arg out: the previous value, before we were called
		 * return: 0 means OK, otherwise not OK
		 */
		retval = kbus_set_report_binds(priv, dev, arg);
		break;

	default:  /* *Should* be redundant, if we got our range checks right */
		retval = -ENOTTY;
		break;
	}

	up(&dev->sem);
	return retval;
}

/*
 * Try sending the (current waiting to be sent) message
 *
 * Returns true if the message has either been successfully sent, or an error
 * occcurred (which has been dealt with) and there is no longer a current
 * message.
 *
 * Returns false if we hit EAGAIN (again) and we're still trying to send the
 * current message.
 */
static int kbus_poll_try_send_again(struct kbus_private_data	*priv,
				    struct kbus_dev		*dev)
{
	int retval;
	struct kbus_message_header *msg;

	msg = (struct kbus_message_header *)priv->write_msg;

	retval = kbus_write_to_recipients(priv, dev, msg);

	switch (-retval) {
	case 0:		/* All is well, nothing to do */
		break;
	case EAGAIN:	/* Still blocked by *someone* - nowt to do */
		break;
	case EADDRNOTAVAIL:
		/*
		 * It's a Request and there's no Replier (presumably there was
		 * when the initial SEND was done, but now they've gone away).
		 * A Request *needs* a Reply...
		 */
		kbus_push_synthetic_message(dev, 0,
					    msg->from, msg->id,
					    "$.KBUS.Replier.Disappeared");
		retval = 0;
		break;
	default:
		/*
		 * Send *failed* - what can we do?
		 * Not much, perhaps, but we must ensure that a Request gets
		 * (some sort of) reply
		 */
		if (msg->flags & KBUS_BIT_WANT_A_REPLY) {
			kbus_push_synthetic_message(dev, 0,
						    msg->from, msg->id,
						    "$.KBUS.ErrorSending");
		}
		retval = 0;
		break;
	}

	if (retval == 0) {
		kbus_discard(priv);
		return true;
	} else {
		return false;
	}
}

static unsigned int kbus_poll(struct file *filp, poll_table *wait)
{
	struct kbus_private_data	*priv = filp->private_data;
	struct kbus_dev			*dev = priv->dev;
	unsigned mask = 0;

	down(&dev->sem);

#if VERBOSE_DEBUG
	if (priv->dev->verbose) {
		printk(KERN_DEBUG "kbus: %u/%u POLL\n",dev->index,priv->id);
	}
#endif

	/*
	 * Did I wake up because there's a message available to be read?
	 */
	if (priv->message_count != 0)
		mask |= POLLIN | POLLRDNORM; /* readable */

	/*
	 * Did I wake up because someone said they had space for a message on
	 * their message queue (where there wasn't space before)?
	 *
	 * And if that is the case, if we're opened for write and have a
	 * message waiting to be sent, can we now send it?
	 *
	 * The simplest way to find out is just to try again.
	 */
	if (filp->f_mode & FMODE_WRITE) {
		int      writable = true;
		if (priv->sending) {
			writable = kbus_poll_try_send_again(priv,dev);
		}
		if (writable)
			mask |= POLLOUT | POLLWRNORM;
	}

	/* Wait until someone has a message waiting to be read */
	poll_wait(filp, &priv->read_wait, wait);

	/* Wait until someone has a space into which a message can be pushed */
	if (priv->sending)
		poll_wait(filp, &dev->write_wait, wait);

	up(&dev->sem);
	return mask;
}

/* File operations for /dev/kbus<n> */
struct file_operations kbus_fops = {
	.owner   = THIS_MODULE,
	.read    = kbus_read,
	.write   = kbus_write,
	.ioctl   = kbus_ioctl,
	.poll    = kbus_poll,
	.open    = kbus_open,
	.release = kbus_release,
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
	INIT_LIST_HEAD(&dev->open_ksock_list);

	init_waitqueue_head(&dev->write_wait);

	dev->next_file_id = 0;
	dev->next_msg_serial_num = 0;

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
	kbus_forget_all_open_ksocks(dev);
}

/* ========================================================================= */
/* PROC */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *kbus_proc_dir;
static struct proc_dir_entry *kbus_proc_file_bindings;
static struct proc_dir_entry *kbus_proc_file_stats;

/*
 * Report on the current bindings, via /proc/kbus/bindings
 */

static int kbus_binding_seq_show(struct seq_file *s, void *v)
{
	int ii;

	/* We report on all of the KBUS devices */
	for (ii=0; ii<kbus_num_devices; ii++) {
		struct kbus_dev	*dev = kbus_devices[ii];

		struct kbus_message_binding *ptr;
		struct kbus_message_binding *next;

		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;

		seq_printf(s,
			   "# <device> is bound to <KSock-ID> in <process-PID>"
			   " as <Replier|Listener> for <message-name>\n");

		list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
			seq_printf(s,
				   "%3u: %8u %8lu  %c  %.*s\n",
				   dev->index,
				   ptr->bound_to_id,
				   (long unsigned)ptr->bound_to->pid,
				   (ptr->is_replier?'R':'L'),
				   ptr->name_len,
				   ptr->name);
		}

		up(&dev->sem);
	}
	return 0;
}

static int kbus_proc_bindings_open(struct inode *inode, struct file *file)
{
	return single_open(file, kbus_binding_seq_show, NULL);
}

static struct file_operations kbus_proc_binding_file_ops = {
	.owner   = THIS_MODULE,
	.open    = kbus_proc_bindings_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static struct proc_dir_entry
	*kbus_create_proc_binding_file(struct proc_dir_entry *directory)
{
	struct proc_dir_entry *entry = create_proc_entry("bindings", 0, directory);
	if (entry) {
		entry->proc_fops = &kbus_proc_binding_file_ops;
	}
	return entry;
}

/*
 * Report on whatever statistics seem like they might be useful,
 * via /proc/kbus/stats
 */

static int kbus_stats_seq_show(struct seq_file *s, void *v)
{
	int ii;

	/* We report on all of the KBUS devices */
	for (ii=0; ii<kbus_num_devices; ii++) {
		struct kbus_dev	*dev = kbus_devices[ii];

		struct kbus_private_data *ptr;
		struct kbus_private_data *next;

		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;

		seq_printf(s, "dev %2u: next file %u next msg %u\n",
			   dev->index,
			   dev->next_file_id,dev->next_msg_serial_num);

		list_for_each_entry_safe(ptr, next, &dev->open_ksock_list, list) {

			uint32_t	left = kbus_lenleft(ptr);
			uint32_t	total;
			if (ptr->read.hdr)
				total = KBUS_ENTIRE_MSG_LEN(ptr->read.hdr->name_len,
							    ptr->read.hdr->data_len);
			else
				total = 0;

			seq_printf(s,
				   "        ksock %u last msg %u:%u queue %u of %u\n"
				   "              read byte %u of %u, wrote byte %u (max %u), %ssending\n"
				   "              outstanding requests %u (size %u, max %u), unsent replies %u (max %u)\n",
				   ptr->id,
				   ptr->last_msg_id_sent.network_id,
				   ptr->last_msg_id_sent.serial_num,
				   ptr->message_count, ptr->max_messages,
				   (total-left), total,
				   ptr->write_msg_len, ptr->write_msg_size,
				   ptr->sending?"":"not ",
				   ptr->outstanding_requests.count,
				   ptr->outstanding_requests.size,
				   ptr->outstanding_requests.max_count,
				   ptr->num_replies_unsent,
				   ptr->max_replies_unsent);
		}
		up(&dev->sem);
	}

	return 0;
}

static int kbus_proc_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, kbus_stats_seq_show, NULL);
}

static struct file_operations kbus_proc_stats_file_ops = {
	.owner   = THIS_MODULE,
	.open    = kbus_proc_stats_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static struct proc_dir_entry
	*kbus_create_proc_stats_file(struct proc_dir_entry *directory)
{
	struct proc_dir_entry *entry = create_proc_entry("stats", 0, directory);
	if (entry) {
		entry->proc_fops = &kbus_proc_stats_file_ops;
	}
	return entry;
}

/* ========================================================================= */

/*
 * Actually setup /dev/kbus<which>.
 *
 * Returns <which> or a negative error code.
 */
static int kbus_setup_new_device(int which)
{
	struct kbus_dev	*new = NULL;
	dev_t		 this_devno;

	if (which < 0 || which > (MAX_NUM_DEVICES-1)) {
		printk(KERN_ERR "kbus: next device index %d not %d..%d\n",
		       which,MIN_NUM_DEVICES,MAX_NUM_DEVICES);
		return -EINVAL;
	}

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new) return -ENOMEM;

	memset(new, 0, sizeof(*new));

	/* Connect the device up with its operations */
	this_devno = MKDEV(kbus_major,kbus_minor+which);
	kbus_setup_cdev(new, this_devno);
	new->index = which;

	/* ================================================================= */
	/* +++ NB: before kernel 2.6.13, the functions we use were
	 * +++ "simple_class_create" and "simple_class_device_add". They are
	 * +++ documented as such in Linux Device Drivers, 3rd edition. When it
	 * +++ became clear that everyone was using the "simple" API, the
	 * +++ kernel code was refactored to make that the norm.
	 */

	kbus_class_devices[which] = device_create(kbus_class_p, NULL,
						  this_devno, NULL, "kbus%d",
						  which);

	kbus_devices[which] = new;
	return which;
}

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
	result = alloc_chrdev_region(&devno, kbus_minor, MAX_NUM_DEVICES,
				     "kbus");
	/* We're quite happy with dynamic allocation of our major number */
	kbus_major = MAJOR(devno);
	if (result < 0) {
		printk(KERN_WARNING "kbus: Cannot allocate character device region\n");
		return result;
	}

	kbus_devices = kmalloc(MAX_NUM_DEVICES * sizeof(struct kbus_dev *),
			       GFP_KERNEL);
	if (!kbus_devices) {
		printk(KERN_WARNING "kbus: Cannot allocate devices\n");
		unregister_chrdev_region(devno, kbus_num_devices);
		return -ENOMEM;
	}
	memset(kbus_devices, 0, kbus_num_devices * sizeof(struct kbus_dev *));

	/* ================================================================= */
	/* +++ NB: before kernel 2.6.13, the functions we use were
	 * +++ "simple_class_create" and "simple_class_device_add". They are
	 * +++ documented as such in Linux Device Drivers, 3rd edition. When it
	 * +++ became clear that everyone was using the "simple" API, the
	 * +++ kernel code was refactored to make that the norm.
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

	kbus_class_devices = kmalloc(MAX_NUM_DEVICES * sizeof(*kbus_class_devices),
				     GFP_KERNEL);
	if (!kbus_class_devices)
	{
		printk(KERN_ERR "kbus: Error creating kbus class device array\n");
		unregister_chrdev_region(devno, kbus_num_devices);
		class_destroy(kbus_class_p);
		/* XXX Is this enough tidying up? CHECK XXX */
		return -ENOMEM;
	}

	/* And connect up the number of devices we've been asked for */
	for (ii=0; ii<kbus_num_devices; ii++) {
		int res = kbus_setup_new_device(ii);
		if (res < 0) {
			unregister_chrdev_region(devno, kbus_num_devices);
			class_destroy(kbus_class_p);
			return res;
		}
	}

	/* ================================================================= */
	/* Within the /proc/kbus directory, we have: */
	kbus_proc_dir = proc_mkdir("kbus", NULL);
	if (kbus_proc_dir) {
		/* /proc/kbus/bindings -- message name bindings */
		kbus_proc_file_bindings =
			kbus_create_proc_binding_file(kbus_proc_dir);
		/* /proc/kbus/stats -- miscellaneous statistics */
		kbus_proc_file_stats =
			kbus_create_proc_stats_file(kbus_proc_dir);
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
		kbus_teardown_cdev(kbus_devices[ii]);
		kfree(kbus_devices[ii]);
	}
	unregister_chrdev_region(devno, kbus_num_devices);

	if (kbus_proc_dir) {
		if (kbus_proc_file_bindings)
			remove_proc_entry("bindings",kbus_proc_dir);
		if (kbus_proc_file_stats)
			remove_proc_entry("stats",kbus_proc_dir);
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
