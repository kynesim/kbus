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

#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/device.h>	/* device classes (for hotplugging), &c */
#include <linux/cdev.h>		/* registering character devices */
#include <linux/list.h>
#include <linux/ctype.h>	/* for isalnum */
#include <linux/poll.h>
#include <linux/slab.h>		/* for kmalloc, etc. */
#include <linux/sched.h>	/* for current->pid */
#include <linux/uaccess.h>	/* copy_*_user() functions */
#include <asm/page.h>		/* PAGE_SIZE */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <linux/kbus_defns.h>
#include "kbus_internal.h"

static int kbus_num_devices = CONFIG_KBUS_DEF_NUM_DEVICES;

/* Who we are -- devices */
static int kbus_major;	/* 0 => We'll go for dynamic allocation */
static int kbus_minor;	/* 0 => We're happy to start with device 0 */

/* We can't need more than 8 characters of padding, by definition! */
static char *static_zero_padding = "\0\0\0\0\0\0\0\0";
static u32 static_end_guard = KBUS_MSG_END_GUARD;

/* Our actual devices, 0 through kbus_num_devices-1 */
static struct kbus_dev **kbus_devices;

static struct class *kbus_class_p;

/* /proc */
static struct proc_dir_entry *kbus_proc_dir;
static struct proc_dir_entry *kbus_proc_file_bindings;
static struct proc_dir_entry *kbus_proc_file_stats;

/* ========================================================================= */

/* As few foreshadowings as I can get away with */
static struct kbus_private_data *kbus_find_open_ksock(struct kbus_dev *dev,
						      u32 id);

/* I really want this function where it is in the code, so need to foreshadow */
static int kbus_setup_new_device(int which);

/* More or less ditto */
static int kbus_write_to_recipients(struct kbus_private_data *priv,
				    struct kbus_dev *dev,
				    struct kbus_msg *msg);

static void kbus_forget_unbound_unsent_unbind_msgs(struct kbus_private_data
						   *priv,
						   struct kbus_message_binding
						   *binding);

static int kbus_alloc_ref_data(struct kbus_private_data *priv,
			       u32 data_len,
			       struct kbus_data_ptr **ret_ref_data);
/* ========================================================================= */

/* What's the symbolic name of a message part? */
static const char *kbus_msg_part_name(enum kbus_msg_parts p)
{
	switch (p) {
	case KBUS_PART_HDR:	return "HDR";
	case KBUS_PART_NAME:	return "NAME";
	case KBUS_PART_NPAD:	return "NPAD";
	case KBUS_PART_DATA:	return "DATA";
	case KBUS_PART_DPAD:	return "DPAD";
	case KBUS_PART_FINAL_GUARD:	return "FINAL";
	}

	pr_err("kbus: unhandled enum lookup %d in "
		   "kbus_msg_part_name - memory corruption?", p);
	return "???";
}

/* What's the symbolic name of a replier type? */
__maybe_unused
static const char *kbus_replier_type_name(enum kbus_replier_type t)
{
	switch (t) {
	case UNSET:		return "UNSET";
	case WILD_STAR:		return "WILD_STAR";
	case WILD_PERCENT:	return "WILD_PERCENT";
	case SPECIFIC:		return "SPECIFIC";
	}
	pr_err("kbus: unhandled enum lookup %d in "
		   "kbus_replier_type_name - memory corruption?", t);
	return "???";
}

/*
 * Wrap a set of data pointers and lengths in a reference
 */
static struct kbus_data_ptr *kbus_wrap_data_in_ref(int as_pages,
						   unsigned num_parts,
						   unsigned long *parts,
						   unsigned *lengths,
						   unsigned last_page_len)
{
	struct kbus_data_ptr *new = NULL;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	new->as_pages = as_pages;
	new->parts = parts;
	new->lengths = lengths;
	new->num_parts = num_parts;
	new->last_page_len = last_page_len;

	kref_init(&new->refcount);
	return new;
}

/*
 * Increment the reference count for our pointer.
 *
 * Returns the (same) reference, for convenience.
 */
static struct kbus_data_ptr *kbus_raise_data_ref(struct kbus_data_ptr *refdata)
{
	if (refdata != NULL)
		kref_get(&refdata->refcount);
	return refdata;
}

/*
 * Data release callback for data reference pointers. Called when the reference
 * count says to...
 */
static void kbus_release_data_ref(struct kref *ref)
{
	struct kbus_data_ptr *refdata = container_of(ref,
						     struct kbus_data_ptr,
						     refcount);
	if (refdata->parts == NULL) {
		/* Not that I think this can happen */
		pr_err("kbus: Removing data reference,"
		       " but data ptr already freed\n");
	} else {
		int jj;
		if (refdata->as_pages)
			for (jj = 0; jj < refdata->num_parts; jj++)
				free_page((unsigned long)refdata->parts[jj]);
		else
			for (jj = 0; jj < refdata->num_parts; jj++)
				kfree((void *)refdata->parts[jj]);
		kfree(refdata->parts);
		kfree(refdata->lengths);
		refdata->parts = NULL;
		refdata->lengths = NULL;
	}
	kfree(refdata);
}

/*
 * Forget a reference to our pointer, and if no-one cares anymore, free it and
 * its contents.
 */
static void kbus_lower_data_ref(struct kbus_data_ptr *refdata)
{
	if (refdata == NULL)
		return;
	kref_put(&refdata->refcount, kbus_release_data_ref);
}

/*
 * Wrap a string in a reference. Does not take a copy of the string,
 * but note that the release mechanism (triggered when there are no more
 * references to the string) *will* free it.
 */
static struct kbus_name_ptr *kbus_wrap_name_in_ref(char *str)
{
	struct kbus_name_ptr *new = NULL;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	new->name = str;
	kref_init(&new->refcount);
	return new;
}

/*
 * Increment the reference count for a string reference
 *
 * Returns the (same) reference, for convenience.
 */
static struct kbus_name_ptr *kbus_raise_name_ref(struct kbus_name_ptr *refname)
{
	if (refname != NULL)
		kref_get(&refname->refcount);
	return refname;
}

/*
 * Data release callback for string reference pointers.
 * Called when the reference count says to...
 */
static void kbus_release_name_ref(struct kref *ref)
{
	struct kbus_name_ptr *refname = container_of(ref,
						     struct kbus_name_ptr,
						     refcount);
	if (refname->name == NULL) {
		/* Not that I think this can happen */
		pr_err("kbus: Removing name reference,"
		       " but name ptr already freed\n");
	} else {
		kfree(refname->name);
		refname->name = NULL;
	}
	kfree(refname);
}

/*
 * Forget a reference to our string, and if no-one cares anymore, free it and
 * its contents.
 */
static void kbus_lower_name_ref(struct kbus_name_ptr *refname)
{
	if (refname == NULL)
		return;

	kref_put(&refname->refcount, kbus_release_name_ref);
}

/*
 * Return a stab at the next size for an array
 */
static u32 kbus_next_size(u32 old_size)
{
	if (old_size < 16)
		/* For very small numbers, just double */
		return old_size << 1;
	/* Otherwise, try something like the mechanism used for Python
	 * lists - doubling feels a bit over the top */
	return old_size + (old_size >> 3);
}

/* Determine (and return) the next message serial number */
static u32 kbus_next_serial_num(struct kbus_dev *dev)
{
	if (dev->next_msg_serial_num == 0)
		dev->next_msg_serial_num++;
	return dev->next_msg_serial_num++;
}

static int kbus_same_message_id(struct kbus_msg_id *msg_id,
				u32 network_id, u32 serial_num)
{
	return msg_id->network_id == network_id &&
	    msg_id->serial_num == serial_num;
}

static int kbus_init_msg_id_memory(struct kbus_private_data *priv)
{
	struct kbus_msg_id_mem *mem = &priv->outstanding_requests;
	struct kbus_msg_id *ids;

	ids = kmalloc(sizeof(*ids) * KBUS_INIT_MSG_ID_MEMSIZE, GFP_KERNEL);
	if (!ids)
		return -ENOMEM;

	memset(ids, 0, sizeof(*ids) * KBUS_INIT_MSG_ID_MEMSIZE);

	mem->count = 0;
	mem->max_count = 0;
	mem->ids = ids;
	mem->size = KBUS_INIT_MSG_ID_MEMSIZE;
	return 0;
}

static void kbus_empty_msg_id_memory(struct kbus_private_data *priv)
{
	struct kbus_msg_id_mem *mem = &priv->outstanding_requests;

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
static int kbus_remember_msg_id(struct kbus_private_data *priv,
				struct kbus_msg_id *id)
{
	struct kbus_msg_id_mem *mem = &priv->outstanding_requests;
	int ii, which;

	kbus_maybe_dbg(priv->dev, "  %u/%u Remembering outstanding"
		       " request %u:%u (count->%d)\n",
		       priv->dev->index, priv->id,
		       id->network_id, id->serial_num, mem->count + 1);

	/* First, try for an empty slot we can re-use */
	for (ii = 0; ii < mem->size; ii++) {
		if (kbus_same_message_id(&mem->ids[ii], 0, 0)) {
			which = ii;
			goto done;
		}

	}
	/* Otherwise, give in and use a new one */
	if (mem->count == mem->size) {
		u32 old_size = mem->size;
		u32 new_size = kbus_next_size(old_size);

		kbus_maybe_dbg(priv->dev, "  %u/%u XXX outstanding"
			       " request array size %u -> %u\n",
			       priv->dev->index, priv->id, old_size, new_size);

		mem->ids = krealloc(mem->ids,
				    new_size * sizeof(struct kbus_msg_id),
				    GFP_KERNEL);
		if (!mem->ids)
			return -EFAULT;
		for (ii = old_size; ii < new_size; ii++) {
			mem->ids[ii].network_id = 0;
			mem->ids[ii].serial_num = 0;
		}
		mem->size = new_size;
		which = mem->count;
	}
	which = mem->count;
done:
	mem->ids[which] = *id;
	mem->count++;
	if (mem->count > mem->max_count)
		mem->max_count = mem->count;
	return 0;
}

/* Returns 0 if we found it, -1 if we couldn't find it */
static int kbus_find_msg_id(struct kbus_private_data *priv,
			    struct kbus_msg_id *id)
{
	struct kbus_msg_id_mem *mem = &priv->outstanding_requests;
	int ii;
	for (ii = 0; ii < mem->size; ii++) {
		if (!kbus_same_message_id(&mem->ids[ii],
					  id->network_id, id->serial_num))
			continue;
		kbus_maybe_dbg(priv->dev, "  %u/%u Found outstanding "
			       "request %u:%u (count=%d)\n",
			       priv->dev->index, priv->id, id->network_id,
			       id->serial_num, mem->count);
		return 0;
	}
	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Could not find outstanding "
		       "request %u:%u (count=%d)\n",
		       priv->dev->index, priv->id, id->network_id,
		       id->serial_num, mem->count);
	return -1;
}

/* Returns 0 if we found and forgot it, -1 if we couldn't find it */
static int kbus_forget_msg_id(struct kbus_private_data *priv,
			      struct kbus_msg_id *id)
{
	struct kbus_msg_id_mem *mem = &priv->outstanding_requests;
	int ii;
	for (ii = 0; ii < mem->size; ii++) {
		if (!kbus_same_message_id(&mem->ids[ii],
					 id->network_id, id->serial_num))
			continue;

		mem->ids[ii].network_id = 0;
		mem->ids[ii].serial_num = 0;
		mem->count--;
		kbus_maybe_dbg(priv->dev,
			       "  %u/%u Forgot outstanding "
			       "request %u:%u (count<-%d)\n",
			       priv->dev->index, priv->id, id->network_id,
			       id->serial_num, mem->count);

		return 0;
	}
	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Could not forget outstanding "
		       "request %u:%u (count<-%d)\n",
		       priv->dev->index, priv->id, id->network_id,
		       id->serial_num, mem->count);
	return -1;
}

/* A message is a reply iff 'in_reply_to' is non-zero */
static int kbus_message_is_reply(struct kbus_msg *msg)
{
	return !kbus_same_message_id(&msg->in_reply_to, 0, 0);
}

/*
 * Build a KBUS synthetic message/exception. We assume no data.
 *
 * The message built is a 'pointy' message.
 *
 * 'msg_name' is copied.
 *
 * Use kbus_free_message() to free this message when it is finished with.
 */
static struct kbus_msg
*kbus_build_kbus_message(struct kbus_dev *dev,
			 char *msg_name,
			 u32 from,
			 u32 to, struct kbus_msg_id in_reply_to)
{
	struct kbus_msg *new_msg;
	struct kbus_name_ptr *name_ref;

	size_t msg_name_len = strlen(msg_name);
	char *msg_name_copy;

	new_msg = kmalloc(sizeof(*new_msg), GFP_KERNEL);
	if (!new_msg) {
		dev_err(dev->dev, "Cannot kmalloc synthetic message\n");
		return NULL;
	}

	msg_name_copy = kmalloc(msg_name_len + 1, GFP_KERNEL);
	if (!msg_name_copy) {
		dev_err(dev->dev, "Cannot kmalloc synthetic message's name\n");
		kfree(new_msg);
		return NULL;
	}

	strncpy(msg_name_copy, msg_name, msg_name_len);
	msg_name_copy[msg_name_len] = '\0';

	name_ref = kbus_wrap_name_in_ref(msg_name_copy);
	if (!name_ref) {
		dev_err(dev->dev, "Cannot kmalloc synthetic message's string ref\n");
		kfree(new_msg);
		kfree(msg_name_copy);
		return NULL;
	}

	memset(new_msg, 0, sizeof(*new_msg));

	new_msg->from = from;
	new_msg->to = to;
	new_msg->in_reply_to = in_reply_to;
	new_msg->flags = KBUS_BIT_SYNTHETIC;
	new_msg->name_ref = name_ref;
	new_msg->name_len = msg_name_len;

	new_msg->id.serial_num = kbus_next_serial_num(dev);

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
	size_t ii;
	int dot_at = 1;

	if (name_len < 3)
		return 1;

	if (name == NULL || name[0] != '$' || name[1] != '.')
		return 1;

	if (name[name_len - 2] == '.' && name[name_len - 1] == '*')
		name_len -= 2;
	else if (name[name_len - 2] == '.' && name[name_len - 1] == '%')
		name_len -= 2;

	if (name[name_len - 1] == '.')
		return 1;

	for (ii = 2; ii < name_len; ii++) {
		if (name[ii] == '.') {
			if (dot_at == ii - 1)
				return 1;
			dot_at = ii;
		} else if (!isalnum(name[ii]))
			return 1;
	}
	return 0;
}

/*
 * Is a message name wildcarded?
 *
 * We assume it is already checked to be a valid name
 *
 * Returns 1 if it is, 0 if not. In other words, returns 1
 * if the name is not a valid destination.
 */
static int kbus_wildcarded_message_name(char *name, size_t name_len)
{
	return name[name_len - 1] == '*' || name[name_len - 1] == '%';
}

/*
 * Is a message name legitimate for writing/sending?
 *
 * This is an omnibus call of the last two checks, with error output.
 *
 * Returns 0 if it's OK, 1 if it's naughty
 */
static int kbus_invalid_message_name(struct kbus_dev *dev,
				     char *name, size_t name_len)
{
	if (kbus_bad_message_name(name, name_len)) {
		dev_err(dev->dev, "pid %u [%s]"
		       " (send) message name '%.*s' is not allowed\n",
		       current->pid, current->comm, (int)name_len, name);
		return 1;
	}
	if (kbus_wildcarded_message_name(name, name_len)) {
		dev_err(dev->dev, "pid %u [%s]"
		       " (send) sending to wildcards not allowed, "
		       "message name '%.*s'\n",
		       current->pid, current->comm, (int)name_len, name);
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
	size_t other_len = strlen(other);

	if (other[other_len - 1] == '*' || other[other_len - 1] == '%') {
		char *rest = name + other_len - 1;
		size_t rest_len = name_len - other_len + 1;

		/*
		 * If we have '$.Fred.*', then we need at least '$.Fred.X'
		 * to match
		 */
		if (name_len < other_len)
			return false;
		/*
		 * Does the name match all of the wildcard except the
		 * last character?
		 */
		if (strncmp(other, name, other_len - 1))
			return false;

		/* '*' matches anything at all, so we're done */
		if (other[other_len - 1] == '*')
			return true;

		/* '%' only matches if we don't have another dot */
		if (strnchr(rest, rest_len, '.'))
			return false;
		else
			return true;
	} else {
		if (name_len != other_len)
			return false;
		else
			return !strncmp(name, other, name_len);
	}
}

/*
 * Check if a message read by kbus_write() is well formed
 *
 * Return 0 if a message is well-formed, negative otherwise.
 */
static int kbus_check_message_written(struct kbus_dev *dev,
				      struct kbus_write_msg *this)
{
	struct kbus_message_header *user_msg =
	    (struct kbus_message_header *)&this->user_msg;

	if (this == NULL) {
		dev_err(dev->dev, "pid %u [%s]"
		       " Tried to check NULL message\n",
		       current->pid, current->comm);
		return -EINVAL;
	}

	if (user_msg->start_guard != KBUS_MSG_START_GUARD) {
		dev_err(dev->dev, "pid %u [%s]"
		       " message start guard is %08x, not %08x",
		       current->pid, current->comm,
		       user_msg->start_guard, KBUS_MSG_START_GUARD);
		return -EINVAL;
	}
	if (user_msg->end_guard != KBUS_MSG_END_GUARD) {
		dev_err(dev->dev, "pid %u [%s]"
		       " message end guard is %08x, not %08x\n",
		       current->pid, current->comm,
		       user_msg->end_guard, KBUS_MSG_END_GUARD);
		return -EINVAL;
	}

	if (user_msg->name_len == 0) {
		dev_err(dev->dev, "pid %u [%s]"
		       " Message name length is 0\n",
		       current->pid, current->comm);
		return -EINVAL;
	}
	if (user_msg->name_len > KBUS_MAX_NAME_LEN) {
		dev_err(dev->dev, "pid %u [%s]"
		       " Message name length is %u, more than %u\n",
		       current->pid, current->comm,
		       user_msg->name_len, KBUS_MAX_NAME_LEN);
		return -ENAMETOOLONG;
	}

	if (user_msg->name == NULL) {
		if (user_msg->data != NULL) {
			dev_err(dev->dev, "pid %u [%s]"
			       " Message name is inline, data is not\n",
			       current->pid, current->comm);
			return -EINVAL;
		}
	} else {
		if (user_msg->data == NULL && user_msg->data_len != 0) {
			dev_err(dev->dev, "pid %u [%s]"
			       " Message data is inline, name is not\n",
			       current->pid, current->comm);
			return -EINVAL;
		}
	}

	if (user_msg->data_len == 0 && user_msg->data != NULL) {
		dev_err(dev->dev, "pid %u [%s]"
		       " Message data length is 0, but data pointer is set\n",
		       current->pid, current->comm);
		return -EINVAL;
	}

	/* It's not legal to set both ALL_OR_WAIT and ALL_OR_FAIL */
	if ((user_msg->flags & KBUS_BIT_ALL_OR_WAIT) &&
	    (user_msg->flags & KBUS_BIT_ALL_OR_FAIL)) {
		dev_err(dev->dev, "pid %u [%s]"
		       " Message cannot have both ALL_OR_WAIT and "
		       "ALL_OR_FAIL set\n",
		       current->pid, current->comm);
		return -EINVAL;
	}
	return 0;
}

/*
 * Output a description of an in-kernel message
 */
static void kbus_maybe_report_message(struct kbus_dev *dev __maybe_unused,
				      struct kbus_msg *msg __maybe_unused)
{
	if (msg->data_len) {
		struct kbus_data_ptr *data_p = msg->data_ref;
		uint8_t *part0 __maybe_unused = (uint8_t *) data_p->parts[0];
		kbus_maybe_dbg(dev, "=== %u:%u '%.*s'"
		       " to %u from %u in-reply-to %u:%u orig %u,%u "
		       "final %u:%u flags %04x:%04x"
		       " data/%u<in%u> %02x.%02x.%02x.%02x\n",
		       msg->id.network_id, msg->id.serial_num,
		       msg->name_len, msg->name_ref->name,
		       msg->to, msg->from,
		       msg->in_reply_to.network_id, msg->in_reply_to.serial_num,
		       msg->orig_from.network_id, msg->orig_from.local_id,
		       msg->final_to.network_id, msg->final_to.local_id,
		       (msg->flags & 0xFFFF0000) >> 4,
		       (msg->flags & 0x0000FFFF), msg->data_len,
		       data_p->num_parts, part0[0], part0[1], part0[2],
		       part0[3]);
	} else {
		kbus_maybe_dbg(dev, "=== %u:%u '%.*s'"
		       " to %u from %u in-reply-to %u:%u orig %u,%u "
		       "final %u,%u flags %04x:%04x\n",
		       msg->id.network_id, msg->id.serial_num,
		       msg->name_len, msg->name_ref->name,
		       msg->to, msg->from,
		       msg->in_reply_to.network_id, msg->in_reply_to.serial_num,
		       msg->orig_from.network_id, msg->orig_from.local_id,
		       msg->final_to.network_id, msg->final_to.local_id,
		       (msg->flags & 0xFFFF0000) >> 4,
		       (msg->flags & 0x0000FFFF));
	}
}

/*
 * Copy a message, doing whatever is deemed necessary.
 *
 * Copies the message header, and also copies the message name and any
 * data. The message must be a 'pointy' message with reference counted
 * name and data.
 */
static struct kbus_msg *kbus_copy_message(struct kbus_dev *dev,
					  struct kbus_msg *old_msg)
{
	struct kbus_msg *new_msg;

	new_msg = kmalloc(sizeof(*new_msg), GFP_KERNEL);
	if (!new_msg) {
		dev_err(dev->dev, "Cannot kmalloc copy of message header\n");
		return NULL;
	}
	if (!memcpy(new_msg, old_msg, sizeof(*new_msg))) {
		dev_err(dev->dev, "Cannot copy message header\n");
		kfree(new_msg);
		return NULL;
	}

	/* In case of error before we're finished... */
	new_msg->name_ref = NULL;
	new_msg->data_ref = NULL;

	new_msg->name_ref = kbus_raise_name_ref(old_msg->name_ref);

	if (new_msg->data_len)
		/* Take a new reference to the data */
		new_msg->data_ref = kbus_raise_data_ref(old_msg->data_ref);
	return new_msg;
}

/*
 * Free a message.
 *
 * Also dereferences the message name and any message data.
 */
static void kbus_free_message(struct kbus_msg *msg)
{
	if (msg->name_ref)
		kbus_lower_name_ref(msg->name_ref);
	msg->name_len = 0;
	msg->name_ref = NULL;

	if (msg->data_len && msg->data_ref)
		kbus_lower_data_ref(msg->data_ref);

	msg->data_len = 0;
	msg->data_ref = NULL;
	kfree(msg);
}

static void kbus_empty_read_msg(struct kbus_private_data *priv)
{
	struct kbus_read_msg *this = &(priv->read);
	int ii;

	if (this->msg) {
		kbus_free_message(this->msg);
		this->msg = NULL;
	}

	for (ii = 0; ii < KBUS_NUM_PARTS; ii++) {
		this->parts[ii] = NULL;
		this->lengths[ii] = 0;
	}
	this->which = 0;
	this->pos = 0;
	this->ref_data_index = 0;
}

static void kbus_empty_write_msg(struct kbus_private_data *priv)
{
	struct kbus_write_msg *this = &priv->write;
	if (this->msg) {
		kbus_free_message(this->msg);
		this->msg = NULL;
	}

	if (this->ref_name) {
		kbus_lower_name_ref(this->ref_name);
		this->ref_name = NULL;
	}

	if (this->ref_data) {
		kbus_lower_data_ref(this->ref_data);
		this->ref_data = NULL;
	}

	this->is_finished = false;
	this->pos = 0;
	this->which = 0;
}

/*
 * Copy the given message, and add it to the end of the queue.
 *
 * This is the *only* way of adding a message to a queue. It shall remain so.
 *
 * We assume the message has been checked for sanity.
 *
 * 'msg' is the message to add to the queue.
 *
 * 'binding' is a pointer to the KBUS message name binding that caused the
 * message to be added.
 *
 * 'for_replier' is true if this particular message is being pushed to the
 * message's replier's queue. Specifically, it's true if this is a Reply
 * to this Ksock, or a Request aimed at this Ksock (as Replier).
 *
 * Returns 0 if all goes well, or -EFAULT/-ENOMEM if we can't allocate
 * datastructures.
 *
 * May also return negative values if the message is mis-named or malformed,
 * at least at the moment.
 */
static int kbus_push_message(struct kbus_private_data *priv,
			     struct kbus_msg *msg,
			     struct kbus_message_binding *binding,
			     int for_replier)
{
	struct list_head *queue = &priv->message_queue;
	struct kbus_msg *new_msg = NULL;
	struct kbus_message_queue_item *item;

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Pushing message onto queue (%s)\n",
		       priv->dev->index, priv->id,
		       for_replier ? "replier" : "listener");

	/*
	 * 1. Check to see if this Ksock has the "only one copy
	 *    of a message" flag set.
	 * 2. If it does, check if our message (id) is already on
	 *    the queue, and if it is, just skip adding it.
	 *
	 * (this means if the Ksock was destined to get the message
	 * several times, either as Replier and Listener, or as
	 * multiple Listeners to the same message name, it will only
	 * get it once, for this "push")
	 *
	 * If "for_replier" is set we necessarily push the message - see below.
	 */
	if (priv->messages_only_once && !for_replier) {
		/*
		 * 1. We've been asked to only send one copy of a message
		 *    to each Ksock that should receive it.
		 * 2. This is not a Reply (to our Ksock) or a Request (to
		 *    our Ksock as Replier)
		 *
		 * So, given that, has a message with that id already been
		 * added to the message queue?
		 *
		 * (Note that if a message would be included because of
		 * multiple message name bindings, we do not say anything
		 * about which binding we will actually add the message
		 * for - so unbinding later on may or may not cause a
		 * message to go away, in this case.)
		 */
		if (kbus_same_message_id(&priv->msg_id_just_pushed,
					 msg->id.network_id,
					 msg->id.serial_num)) {
			kbus_maybe_dbg(priv->dev,
				       "  %u/%u Ignoring message "
				       "under 'once only' rule\n",
				       priv->dev->index, priv->id);
			return 0;
		}
	}

	new_msg = kbus_copy_message(priv->dev, msg);
	if (!new_msg)
		return -EFAULT;

	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		dev_err(priv->dev->dev, "Cannot kmalloc new message item\n");
		kbus_free_message(new_msg);
		return -ENOMEM;
	}
	kbus_maybe_report_message(priv->dev, new_msg);

	if (for_replier && (KBUS_BIT_WANT_A_REPLY & msg->flags)) {
		/*
		 * This message wants a reply, and is for the message's
		 * replier, so they need to be told that they are to reply to
		 * this message
		 */
		new_msg->flags |= KBUS_BIT_WANT_YOU_TO_REPLY;
		kbus_maybe_dbg(priv->dev,
			       "  Setting WANT_YOU_TO_REPLY, "
			       "flags %08x\n",
			       new_msg->flags);
	} else {
		/*
		 * The recipient is *not* the replier for this message,
		 * so it is not responsible for replying.
		 */
		new_msg->flags &= ~KBUS_BIT_WANT_YOU_TO_REPLY;
	}

	/* And join it up... */
	item->msg = new_msg;
	item->binding = binding;

	/* By default, we're using the list as a FIFO, so we want to add our
	 * new message to the end (just before the first item). However, if the
	 * URGENT flag is set, then we instead want to add it to the start.
	 */
	if (msg->flags & KBUS_BIT_URGENT) {
		kbus_maybe_dbg(priv->dev, "  Message is URGENT\n");
		list_add(&item->list, queue);
	} else {
		list_add_tail(&item->list, queue);
	}

	priv->message_count++;
	priv->msg_id_just_pushed = msg->id;

	if (!kbus_same_message_id(&msg->in_reply_to, 0, 0)) {
		/*
		 * If it's a reply (and this will include a synthetic reply,
		 * since we're checking the "in_reply_to" field) then the
		 * original sender has now had its request satisfied.
		 */
		int retval = kbus_forget_msg_id(priv, &msg->in_reply_to);

		if (retval)
			/* But there's not much we can do about it */
			dev_err(priv->dev->dev,
			       "%u/%u Error forgetting "
			       "outstanding request %u:%u\n",
			       priv->dev->index, priv->id,
			       msg->in_reply_to.network_id,
			       msg->in_reply_to.serial_num);
	}

	/* And indicate that there is something available to read */
	wake_up_interruptible(&priv->read_wait);

	kbus_maybe_dbg(priv->dev,
		       "%u/%u Leaving %d message%s in queue\n",
		       priv->dev->index, priv->id, priv->message_count,
		       priv->message_count == 1 ? "" : "s");

	return 0;
}

/*
 * Generate a synthetic message, and add it to the recipient's message queue.
 *
 * This is to be used when a Reply is not going to be generated
 * by the intended Replier. Since we don't want KBUS itself to block on
 * (trying to) SEND a message to someone not expecting it, I don't think
 * there are any other occasions when it is useful.
 *
 * 'from' is the id of the recipient who has gone away, not received the
 * message, or whatever.
 *
 * 'to' is the 'from' for the message we're bouncing (or whatever). This
 * needs to be local (it cannot be on another network), so we don't specify
 * the network id.
 *
 * 'in_reply_to' should be the message id of that same message.
 *
 * Note that the message is essentially a Reply, so it only goes to the
 * original Sender.
 *
 * Doesn't return anything since I can't think of anything useful to do if it
 * goes wrong.
 */
static void kbus_push_synthetic_message(struct kbus_dev *dev,
					u32 from,
					u32 to,
					struct kbus_msg_id in_reply_to,
					char *name)
{
	struct kbus_private_data *priv = NULL;
	struct kbus_msg *new_msg;

	/* Who *was* the original message to? */
	priv = kbus_find_open_ksock(dev, to);
	if (!priv) {
		dev_err(dev->dev,
		       "%u pid %u [%s] Cannot send synthetic reply to %u,"
		       " as they are gone\n", dev->index, current->pid,
		       current->comm, to);
		return;
	}

	kbus_maybe_dbg(priv->dev, "  %u Pushing synthetic message '%s'"
		       " onto queue for %u\n", dev->index, name, to);

	/*
	 * Note that we do not check if the destination queue is full
	 * - we're going to trust that the "keep enough room in the
	 * message queue for a reply to each request" mechanism does
	 * it's job properly.
	 */

	new_msg = kbus_build_kbus_message(dev, name, from, to, in_reply_to);
	if (!new_msg)
		return;

	(void)kbus_push_message(priv, new_msg, NULL, false);
	/* ignore retval; we can't do anything useful if this goes wrong */

	/* kbus_push_message takes a copy of our message */
	kbus_free_message(new_msg);
}

/*
 * Add the data part to a bind/unbind synthetic message.
 *
 * 'is_bind' is true if this was a "bind" event, false if it was an "unbind".
 *
 * 'name' is the message name (or wildcard) that was bound (or unbound) to.
 *
 * Returns 0 if all goes well, or a negative value if something goes wrong.
 */
static int kbus_add_bind_message_data(struct kbus_private_data *priv,
				      struct kbus_msg *new_msg,
				      u32 is_bind,
				      u32 name_len, char *name)
{
	struct kbus_replier_bind_event_data *data;
	struct kbus_data_ptr *wrapped_data;
	u32 padded_name_len = KBUS_PADDED_NAME_LEN(name_len);
	u32 data_len = sizeof(*data) + padded_name_len;
	u32 rest_len = padded_name_len / 4;
	char *name_p;

	unsigned long *parts;
	unsigned *lengths;

	data = kmalloc(data_len, GFP_KERNEL);
	if (!data) {
		kbus_free_message(new_msg);
		return -ENOMEM;
	}

	name_p = (char *)&data->rest[0];

	data->is_bind = is_bind;
	data->binder = priv->id;
	data->name_len = name_len;

	data->rest[rest_len - 1] = 0;	/* terminating with enough '\0' */
	strncpy(name_p, name, name_len);

	/*
	 * And that data, unfortunately for our simple mindedness,
	 * needs wrapping up in a reference count...
	 *
	 * Note/remember that we are happy for the reference counting
	 * wrapper to "own" our data, and free it when the it is done
	 * with it.
	 *
	 * I'm going to assert that we have less than a PAGE length
	 * of data, so we can simply do:
	 */
	parts = kmalloc(sizeof(*parts), GFP_KERNEL);
	if (!parts) {
		kfree(data);
		return -ENOMEM;
	}
	lengths = kmalloc(sizeof(*lengths), GFP_KERNEL);
	if (!lengths) {
		kfree(parts);
		kfree(data);
		return -ENOMEM;
	}
	lengths[0] = data_len;
	parts[0] = (unsigned long)data;
	wrapped_data =
	    kbus_wrap_data_in_ref(false, 1, parts, lengths, data_len);
	if (!wrapped_data) {
		kfree(lengths);
		kfree(parts);
		kfree(data);
		return -ENOMEM;
	}

	new_msg->data_len = data_len;
	new_msg->data_ref = wrapped_data;
	return 0;
}

/*
 * Create a new Replier Bind Event synthetic message.
 *
 * The initial design of things didn't really expect us to be
 * generating messages with actual data inside the kernel module,
 * so it's all a little bit more complicated than it might otherwise
 * be, and there's some playing with things directly that might best
 * be done otherwise (notably, sorting out the wrapping up of the
 * data in a reference count). I think that's excusable given this
 * should be the only sort of message we ever generate with actual
 * data (or so I believe).
 *
 * Returns the new message, or NULL.
 */
static struct kbus_msg
*kbus_new_synthetic_bind_message(struct kbus_private_data *priv,
				 u32 is_bind,
				 u32 name_len, char *name)
{
	ssize_t retval = 0;
	struct kbus_msg *new_msg;
	struct kbus_msg_id in_reply_to = { 0, 0 };	/* no-one */

	kbus_maybe_dbg(priv->dev,
		       "  %u Creating synthetic bind message for '%s'"
		       " (%s)\n", priv->dev->index, name,
		       is_bind ? "bind" : "unbind");

	new_msg = kbus_build_kbus_message(priv->dev,
					  KBUS_MSG_NAME_REPLIER_BIND_EVENT,
					  0, 0, in_reply_to);
	if (!new_msg)
		return NULL;

	/*
	 * What happens if any one of the listeners can't receive the message
	 * because they don't have room in their queues?
	 *
	 * If we flag the message as ALL_OR_FAIL, then if we can't deliver
	 * to all of the listeners who care, we will get -EBUSY returned to
	 * us, which we shall then return as -EAGAIN (people expect to check
	 * for -EAGAIN to find out if they should, well, try again).
	 *
	 * In this scenario, the user needs to catch a "bind"/"unbind" return
	 * of -EAGAIN and realise that it needs to try again.
	 */
	new_msg->flags |= KBUS_BIT_ALL_OR_FAIL;

	/*
	 * That gave us the basis of the message, but now we need to add in
	 * its meaning.
	 */
	retval = kbus_add_bind_message_data(priv, new_msg, is_bind,
					    name_len, name);
	if (retval < 0) {
		kbus_free_message(new_msg);
		return NULL;
	}

	return new_msg;
}

/*
 * Generate a bind/unbind synthetic message, and broadcast it.
 *
 * This is for use when we have been asked to announce when a Replier binds or
 * unbinds.
 *
 * 'priv' is the sender - the entity that is doing the actual bind/unbind.
 *
 * 'is_bind' is true if this was a "bind" event, false if it was an "unbind".
 *
 * 'name' is the message name (or wildcard) that was bound (or unbound) to.
 *
 * Returns 0 if all goes well, or a negative value if something goes wrong,
 * notably -EAGAIN if we couldn't send the message to ALL the Listeners who
 * have bound to receive it.
 */
static int kbus_push_synthetic_bind_message(struct kbus_private_data *priv,
					    u32 is_bind,
					    u32 name_len, char *name)
{

	ssize_t retval = 0;
	struct kbus_msg *new_msg;

	kbus_maybe_dbg(priv->dev,
		       "  %u Pushing synthetic bind message for '%s'"
		       " (%s) onto queue\n", priv->dev->index, name,
		       is_bind ? "bind" : "unbind");

	new_msg =
	    kbus_new_synthetic_bind_message(priv, is_bind, name_len, name);
	if (new_msg == NULL)
		return -ENOMEM;

	kbus_maybe_report_message(priv->dev, new_msg);
	kbus_maybe_dbg(priv->dev,
		       "Writing synthetic message to "
		       "recipients\n");

	retval = kbus_write_to_recipients(priv, priv->dev, new_msg);
	/*
	 * kbus_push_message takes a copy of our message data, so we
	 * must remember to free ours. Since we've made sure that it
	 * looks just like a user-generated message (i.e., the name
	 * can be freed as well as the data), this is fairly simple.
	 */
	kbus_free_message(new_msg);

	/*
	 * Because we used ALL_OR_FAIL, we will get -EBUSY back if we FAIL,
	 * but we want to tell the user -EAGAIN, since they *should* try
	 * again later.
	 */
	if (retval == -EBUSY)
		retval = -EAGAIN;

	return retval;
}

/*
 * Pop the next message off our queue.
 *
 * Returns a pointer to the message, or NULL if there is no next message.
 */
static struct kbus_msg *kbus_pop_message(struct kbus_private_data *priv)
{
	struct list_head *queue = &priv->message_queue;
	struct kbus_message_queue_item *item;
	struct kbus_msg *msg = NULL;

	kbus_maybe_dbg(priv->dev, "  %u/%u Popping message from queue\n",
		       priv->dev->index, priv->id);

	if (list_empty(queue))
		return NULL;

	/* Retrieve the next message */
	item = list_first_entry(queue, struct kbus_message_queue_item, list);

	/* And straightway remove it from the list */
	list_del(&item->list);

	priv->message_count--;

	msg = item->msg;
	kfree(item);

	/* If doing that made us go from no-room to some-room, wake up */
	if (priv->message_count == (priv->max_messages - 1))
		wake_up_interruptible(&priv->dev->write_wait);

	kbus_maybe_report_message(priv->dev, msg);
	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Leaving %d message%s in queue\n",
		       priv->dev->index, priv->id, priv->message_count,
		       priv->message_count == 1 ? "" : "s");

	return msg;
}

/*
 * Empty a message queue. Send synthetic messages for any outstanding
 * request messages that are now not going to be delivered/replied to.
 */
static void kbus_empty_message_queue(struct kbus_private_data *priv)
{
	struct list_head *queue = &priv->message_queue;
	struct kbus_message_queue_item *ptr;
	struct kbus_message_queue_item *next;

	kbus_maybe_dbg(priv->dev, "  %u/%u Emptying message queue\n",
		       priv->dev->index, priv->id);

	list_for_each_entry_safe(ptr, next, queue, list) {
		struct kbus_msg *msg = ptr->msg;
		int is_OUR_request = (KBUS_BIT_WANT_YOU_TO_REPLY & msg->flags);

		kbus_maybe_report_message(priv->dev, msg);

		/*
		 * If it wanted a reply (from us). let the sender know it's
		 * going away (but take care not to send a message to
		 * ourselves, by accident!)
		 */
		if (is_OUR_request && msg->to != priv->id)
			kbus_push_synthetic_message(priv->dev, priv->id,
					    msg->from, msg->id,
					    KBUS_MSG_NAME_REPLIER_GONEAWAY);

		/* Remove it from the list */
		list_del(&ptr->list);
		/* And forget all about it... */
		kbus_free_message(ptr->msg);

		priv->message_count--;
	}

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Leaving %d message%s in queue\n",
		       priv->dev->index, priv->id, priv->message_count,
		       priv->message_count == 1 ? "" : "s");
}

/*
 * Add a message to the list of messages read by the replier, but still needing
 * a reply.
 */
static int kbus_reply_needed(struct kbus_private_data *priv,
			     struct kbus_msg *msg)
{
	struct list_head *queue = &priv->replies_unsent;
	struct kbus_unreplied_item *item;

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Adding message %u:%u to unsent "
		       "replies list\n",
		       priv->dev->index, priv->id, msg->id.network_id,
		       msg->id.serial_num);

	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		dev_err(priv->dev->dev, "Cannot kmalloc reply-needed item\n");
		return -ENOMEM;
	}

	item->id = msg->id;
	item->from = msg->from;
	item->name_len = msg->name_len;
	/*
	 * It seems sensible to use a reference to the name. I believe
	 * we are safe to do this because we have the message "in hand".
	 */
	item->name_ref = kbus_raise_name_ref(msg->name_ref);

	list_add(&item->list, queue);

	priv->num_replies_unsent++;

	if (priv->num_replies_unsent > priv->max_replies_unsent)
		priv->max_replies_unsent = priv->num_replies_unsent;

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Leaving %d message%s unreplied-to\n",
		       priv->dev->index, priv->id, priv->num_replies_unsent,
		       priv->num_replies_unsent == 1 ? "" : "s");

	return 0;
}

/*
 * Remove a message from the list of (read) messages needing a reply
 *
 * Returns 0 on success, -1 if it could not find the message
 */
static int kbus_reply_now_sent(struct kbus_private_data *priv,
			       struct kbus_msg_id *msg_id)
{
	struct list_head *queue = &priv->replies_unsent;
	struct kbus_unreplied_item *ptr;
	struct kbus_unreplied_item *next;

	list_for_each_entry_safe(ptr, next, queue, list) {
		if (!kbus_same_message_id(&ptr->id,
					 msg_id->network_id,
					 msg_id->serial_num))
			continue;

		kbus_maybe_dbg(priv->dev,
		       "  %u/%u Reply to %u:%u %.*s now sent\n",
		       priv->dev->index, priv->id, msg_id->network_id,
		       msg_id->serial_num, ptr->name_len, ptr->name_ref->name);

		/* Remove it from the list */
		list_del(&ptr->list);
		/* And forget all about it... */
		kbus_lower_name_ref(ptr->name_ref);
		kfree(ptr);

		priv->num_replies_unsent--;

		kbus_maybe_dbg(priv->dev,
		       "  %u/%u Leaving %d message%s unreplied-to\n",
		       priv->dev->index, priv->id, priv->num_replies_unsent,
		       priv->num_replies_unsent == 1 ? "" : "s");

		return 0;
	}

	dev_err(priv->dev->dev, "%u/%u Could not find message %u:%u in unsent "
	       "replies list\n",
	       priv->dev->index, priv->id, msg_id->network_id,
	       msg_id->serial_num);
	return -1;
}

/*
 * Empty our "replies unsent" queue. Send synthetic messages for any
 * request messages that are now not going to be replied to.
 */
static void kbus_empty_replies_unsent(struct kbus_private_data *priv)
{
	struct list_head *queue = &priv->replies_unsent;
	struct kbus_unreplied_item *ptr;
	struct kbus_unreplied_item *next;

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Emptying unreplied messages list\n",
		       priv->dev->index, priv->id);

	list_for_each_entry_safe(ptr, next, queue, list) {

		kbus_push_synthetic_message(priv->dev, priv->id,
					    ptr->from, ptr->id,
					    KBUS_MSG_NAME_REPLIER_IGNORED);

		/* Remove it from the list */
		list_del(&ptr->list);
		/* And forget all about it... */
		kbus_lower_name_ref(ptr->name_ref);
		kfree(ptr);

		priv->num_replies_unsent--;
	}

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Leaving %d message%s unreplied-to\n",
		       priv->dev->index, priv->id, priv->num_replies_unsent,
		       priv->num_replies_unsent == 1 ? "" : "s");
}

/*
 * Find out who, if anyone, is bound as a replier to the given message name.
 *
 * Returns 1 if we found a replier, 0 if we did not (but all went well), and
 * a negative value if something went wrong.
 */
static int kbus_find_replier(struct kbus_dev *dev,
			     struct kbus_private_data **bound_to,
			     u32 name_len, char *name)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	/* We don't want anyone writing to the list whilst we do this */
	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		/*
		 * We are only interested in a replier binding to the name.
		 * We *could* check for the name and then check for
		 * reply-ness - if we found a name match that was *not* a
		 * replyer, then we'd have finished. However, checking the
		 * name is expensive, and I rather assume that a caller is
		 * only checking if they expect a positive result, so it's
		 * simpler to do a lazier check.
		 */
		if (!ptr->is_replier || ptr->name_len != name_len ||
		    strncmp(name, ptr->name, name_len))
			continue;

		kbus_maybe_dbg(dev, "  %u '%.*s' has replier %u\n",
			       dev->index, ptr->name_len, ptr->name,
			       ptr->bound_to_id);
		*bound_to = ptr->bound_to;
		return 1;
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
 * Returns the number of listeners found (i.e., the length of the array), or a
 * negative value if something went wrong. This is a bit clumsy, because the
 * caller needs to check the return value *and* the 'replier' value, but there
 * is only one caller, so...
 */
static int kbus_find_listeners(struct kbus_dev *dev,
			       struct kbus_message_binding **listeners[],
			       struct kbus_message_binding **replier,
			       u32 name_len, char *name)
{
	int count = 0;
	int array_size = KBUS_INIT_LISTENER_ARRAY_SIZE;
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	enum kbus_replier_type replier_type = UNSET;
	enum kbus_replier_type new_replier_type = UNSET;

	kbus_maybe_dbg(dev,
		       "  Looking for listeners/repliers for '%.*s'\n",
		       name_len, name);

	*listeners =
	    kmalloc(array_size * sizeof(struct kbus_message_binding *),
		    GFP_KERNEL);
	if (!(*listeners))
		return -ENOMEM;

	*replier = NULL;

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {

		if (!kbus_message_name_matches(name, name_len, ptr->name))
			continue;

		kbus_maybe_dbg(dev, "     Name '%.*s' matches "
			       "'%s' for %s %u\n",
			       name_len, name, ptr->name,
			       ptr->is_replier ? "replier" : "listener",
			       ptr->bound_to_id);

		if (ptr->is_replier) {
			/* It *may* be the replier for this message */
			size_t last_char = strlen(ptr->name) - 1;
			if (ptr->name[last_char] == '*')
				new_replier_type = WILD_STAR;
			else if (ptr->name[last_char] == '%')
				new_replier_type = WILD_PERCENT;
			else
				new_replier_type = SPECIFIC;

			kbus_maybe_dbg(dev,
				"     ..previous replier was %u "
				"(%s), looking at %u (%s)\n",
				((*replier) == NULL ? 0 :
					(*replier)->bound_to_id),
				kbus_replier_type_name(replier_type),
				ptr->bound_to_id,
				kbus_replier_type_name(new_replier_type));

			/*
			 * If this is the first replier, just remember
			 * it. Otherwise, if it's more specific than
			 * our previous replier, remember it instead.
			 */
			if (*replier == NULL ||
			    new_replier_type > replier_type) {

				if (*replier)
					kbus_maybe_dbg(dev,
					   "     ..new replier %u (%s)\n",
					   ptr->bound_to_id,
					   kbus_replier_type_name(
					   new_replier_type));

				*replier = ptr;
				replier_type = new_replier_type;
			} else {
			    if (*replier)
				kbus_maybe_dbg(dev,
				       "     ..keeping replier %u (%s)\n",
				       (*replier)->bound_to_id,
				       kbus_replier_type_name(replier_type));
			}
		} else {
			/* It is a listener */
			if (count == array_size) {
				u32 new_size = kbus_next_size(array_size);

				kbus_maybe_dbg(dev, "     XXX listener "
				       "array size %d -> %d\n",
				       array_size, new_size);

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

	kbus_maybe_dbg(dev, "     Found %d listener%s%s for '%.*s'\n",
		       count, (count == 1 ? "" : "s"),
		       (*replier == NULL ? "" : " and a replier"),
		       name_len, name);

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
static int kbus_remember_binding(struct kbus_dev *dev,
				 struct kbus_private_data *priv,
				 u32 replier,
				 u32 name_len, char *name)
{
	int retval = 0;
	struct kbus_message_binding *new;

	/* If we want a replier, and there already is one, we lose */
	if (replier) {
		struct kbus_private_data *reply_to;
		retval = kbus_find_replier(dev, &reply_to, name_len, name);
		/*
		 * "Address in use" isn't quite right, but lets the caller
		 * have some hope of telling what went wrong, and this is a
		 * useful case to distinguish.
		 */
		if (retval == 1) {
			kbus_maybe_dbg(dev,
				       "%u/%u CANNOT BIND '%.*s' as "
				       "replier, already bound\n",
				       dev->index, priv->id,
				       name_len, name);
			return -EADDRINUSE;
		}
	}

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new->bound_to = priv;
	new->bound_to_id = priv->id;	/* Useful shorthand? */
	new->is_replier = replier;
	new->name_len = name_len;
	new->name = name;

	if (replier && dev->report_replier_binds) {
		/*
		 * We've been asked to announce when a Replier binds.
		 * If we can't tell all the Listeners who care, we want
		 * to give up, rather than tell some of them, and then
		 * bind anyway.
		 */
		retval = kbus_push_synthetic_bind_message(priv, true,
							  name_len, name);
		if (retval != 0) {	/* Hopefully, just -EBUSY */
			kfree(new);
			return retval;
		}
	}

	list_add(&new->list, &dev->bound_message_list);
	return 0;
}

/*
 * Find a particular binding.
 *
 * Return a pointer to the binding, or NULL if it was not found.
 */
static struct kbus_message_binding
*kbus_find_binding(struct kbus_dev *dev,
		   struct kbus_private_data *priv,
		   u32 replier, u32 name_len, char *name)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		if (priv != ptr->bound_to)
			continue;
		if (replier != ptr->is_replier)
			continue;
		if (name_len != ptr->name_len)
			continue;
		if (strncmp(name, ptr->name, name_len))
			continue;

		kbus_maybe_dbg(priv->dev, "  %u/%u Found %c '%.*s'\n",
			       dev->index, priv->id,
			       (ptr->is_replier ? 'R' : 'L'),
			       ptr->name_len, ptr->name);
		return ptr;
	}
	return NULL;
}

/*
 * Forget any messages (in our queue) that were only in the queue because of
 * the binding we're removing.
 *
 * If the message was a request (needing a reply) generate an appropriate
 * synthetic message.
 */
static void kbus_forget_matching_messages(struct kbus_private_data *priv,
					  struct kbus_message_binding *binding)
{
	struct list_head *queue = &priv->message_queue;
	struct kbus_message_queue_item *ptr;
	struct kbus_message_queue_item *next;

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Forgetting matching messages\n",
		       priv->dev->index, priv->id);

	list_for_each_entry_safe(ptr, next, queue, list) {
		struct kbus_msg *msg = ptr->msg;
		int is_OUR_request = (KBUS_BIT_WANT_YOU_TO_REPLY & msg->flags);

		/*
		 * If this message was not added to the queue because of this
		 * binding, then we are not interested in it...
		 */
		if (ptr->binding != binding)
			continue;

		kbus_maybe_dbg(priv->dev,
				"  Deleting message from queue\n");
		kbus_maybe_report_message(priv->dev, msg);

		/*
		 * If it wanted a reply (from us). let the sender know it's
		 * going away (but take care not to send a message to
		 * ourselves, by accident!)
		 */
		if (is_OUR_request && msg->to != priv->id) {

			kbus_maybe_dbg(priv->dev, "  >>> is_OUR_request,"
				       " sending fake reply\n");
			kbus_maybe_report_message(priv->dev, msg);
			kbus_push_synthetic_message(priv->dev, priv->id,
					    msg->from, msg->id,
					    KBUS_MSG_NAME_REPLIER_UNBOUND);
		}

		/* Remove it from the list */
		list_del(&ptr->list);
		/* And forget all about it... */
		kbus_free_message(ptr->msg);

		priv->message_count--;

		/* If that made us go from no-room to some-room, wake up */
		if (priv->message_count == (priv->max_messages - 1))
			wake_up_interruptible(&priv->dev->write_wait);
	}

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Leaving %d message%s in queue\n",
		       priv->dev->index, priv->id, priv->message_count,
		       priv->message_count == 1 ? "" : "s");
}

/*
 * Remove an existing binding.
 *
 * Returns 0 if all went well, a negative value if it did not.
 */
static int kbus_forget_binding(struct kbus_dev *dev,
			       struct kbus_private_data *priv,
			       u32 replier, u32 name_len, char *name)
{
	struct kbus_message_binding *binding;

	binding = kbus_find_binding(dev, priv, replier, name_len, name);
	if (binding == NULL) {
		kbus_maybe_dbg(priv->dev,
			       "  %u/%u Could not find/unbind "
			       "%u %c '%.*s'\n",
			       dev->index, priv->id, priv->id,
			       (replier ? 'R' : 'L'), name_len, name);
		return -EINVAL;
	}

	if (replier && dev->report_replier_binds) {

		/*
		 * We want to send a message indicating that we've unbound
		 * the Replier for this message.
		 *
		 * If we can't tell all the Listeners who're listening for this
		 * message, we want to give up, rather then tell some of them,
		 * and then unbind anyway.
		 */
		int retval = kbus_push_synthetic_bind_message(priv, false,
							      name_len, name);
		if (retval != 0)	/* Hopefully, just -EBUSY */
			return retval;
		/*
		 * Note that if we were ourselves listening for replier bind
		 * events, then we will ourselves get the message announcing
		 * we're about to unbind.
		 */
	}

	kbus_maybe_dbg(priv->dev, "  %u/%u Unbound %u %c '%.*s'\n",
		       dev->index, priv->id,
		       binding->bound_to_id,
		       (binding->is_replier ? 'R' : 'L'),
		       binding->name_len, binding->name);

	/* And forget any messages we now shouldn't receive */
	kbus_forget_matching_messages(priv, binding);

	/*
	 * Maybe including any set-aside Replier Unbind Events...
	 */
	if (!strncmp(KBUS_MSG_NAME_REPLIER_BIND_EVENT, binding->name,
		     binding->name_len))
		kbus_forget_unbound_unsent_unbind_msgs(priv, binding);

	/*
	 * We carefully don't try to do anything about requests that
	 * have already been read - the fact that the user has unbound
	 * from receiving new messages with this name doesn't imply
	 * anything about whether they're going to reply to requests
	 * (with that name) which they've already read.
	 */

	/* And remove the binding once that has been done. */
	list_del(&binding->list);
	kfree(binding->name);
	kfree(binding);
	return 0;
}

/*
 * Add a (copy of a) message to the "unsent Replier Unbind Event" list
 *
 * 'priv' is who we are trying to send to, 'msg' is the message we were
 * trying to send.
 *
 * Returns 0 if all went well, a negative value if it did not.
 */
static int kbus_remember_unsent_unbind_event(struct kbus_dev *dev,
					     struct kbus_private_data *priv,
					     struct kbus_msg *msg,
					     struct kbus_message_binding
					     *binding)
{
	struct kbus_unsent_message_item *new;
	struct kbus_msg *new_msg = NULL;

	kbus_maybe_dbg(priv->dev,
		       "  %u Remembering unsent unbind event "
		       "%u '%.*s' to %u\n",
		       dev->index, dev->unsent_unbind_msg_count, msg->name_len,
		       msg->name_ref->name, priv->id);

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new_msg = kbus_copy_message(dev, msg);
	if (!new_msg) {
		kfree(new);
		return -EFAULT;
	}

	new->send_to = priv;
	new->send_to_id = priv->id;	/* Useful shorthand? */
	new->msg = new_msg;
	new->binding = binding;

	/*
	 * The order should be the same as a normal message queue,
	 * so add to the end...
	 */
	list_add_tail(&new->list, &dev->unsent_unbind_msg_list);
	dev->unsent_unbind_msg_count++;
	return 0;
}

/*
 * Return true if this listener already has a "gone tragic" message.
 *
 * Look at the end of the unsent Replier Unbind Event message list, to see
 * if the given listener already has a "gone tragic" message (since if it
 * does, we will not want to add another).
 */
static int kbus_listener_already_got_tragic_msg(struct kbus_dev *dev,
						struct kbus_private_data
						*listener)
{
	struct kbus_unsent_message_item *ptr;
	struct kbus_unsent_message_item *next;

	kbus_maybe_dbg(dev,
		       "  %u Checking for 'gone tragic' event for %u\n",
		       dev->index, listener->id);

	list_for_each_entry_safe_reverse(ptr, next,
					 &dev->unsent_unbind_msg_list, list) {

		if (kbus_message_name_matches(
					ptr->msg->name_ref->name,
					ptr->msg->name_len,
					KBUS_MSG_NAME_REPLIER_BIND_EVENT))
			/*
			 * If we get a Replier Bind Event, then we're past all
			 * the "tragic world" messages
			 */
			break;
		if (ptr->send_to_id == listener->id) {
			kbus_maybe_dbg(dev, "  %u Found\n", dev->index);
			return true;
		}
	}

	kbus_maybe_dbg(dev, "  %u Not found\n", dev->index);
	return false;
}

/*
 * Report a Replier Bind Event for unbinding from the given message name,
 * in such a way that we do not lose the message even if we can't send it
 * right away.
 */
static void kbus_safe_report_unbinding(struct kbus_private_data *priv,
				       u32 name_len, char *name)
{
	/* 1. Generate a new unbinding event message
	 * 2. Try sending it to everyone who cares
	 * 3. If that failed, then find out who *does* care
	 * 4. Is there room for that many messages on the set-aside list?
	 * 5. If there is, add (a copy of) the message for each
	 * 6. If there is not, set the "tragic" flag, and add (a copy of)
	 *    the "world gone tragic" message for each
	 * 7. If we've added something to the set-aside list, then set
	 *    the "maybe got something on the set-aside list" flag for
	 *    each recipient. */

	struct kbus_msg *msg;
	struct kbus_message_binding **listeners = NULL;
	struct kbus_message_binding *replier = NULL;
	int retval = 0;
	int num_listeners;
	int ii;

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Safe report unbinding of '%.*s'\n",
		       priv->dev->index, priv->id, name_len, name);

	/* Generate the message we'd *like* to send */
	msg = kbus_new_synthetic_bind_message(priv, false, name_len, name);
	if (msg == NULL)
		return;	/* There is nothing sensible to do here */

	/* If we're lucky, we can just send it */
	retval = kbus_write_to_recipients(priv, priv->dev, msg);
	if (retval != -EBUSY)
		goto done_sending;

	/*
	 * So at least one of the people we were trying to send to was not able
	 * to take the message, presumably because their message queue is full.
	 * Thus we need to put aside one copy of the message for each
	 * recipient, to be delivered when it *can* be received.
	 *
	 * So before we do anything else, we need to know who those recipients
	 * are.
	 */

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Need to add messages to set-aside list\n",
		       priv->dev->index, priv->id);

	/*
	 * We're expecting some listeners, but no replier.
	 * Since this is a duplicate of what we did in kbus_write_to_recipients,
	 * and since our ksock is locked whilst we're working, we can assume
	 * that we should get the same result. For the sake of completeness,
	 * check the error return anyway, but I'm not going to worry about
	 * whether we suddenly have a replier popping up unexpectedly...
	 */
	num_listeners = kbus_find_listeners(priv->dev, &listeners, &replier,
					    msg->name_len, msg->name_ref->name);
	if (num_listeners < 0) {
		kbus_maybe_dbg(priv->dev,
			       "  Error %d finding listeners\n",
			       num_listeners);
		retval = num_listeners;
		goto done_sending;
	}

	if (priv->dev->unsent_unbind_is_tragic ||
	    (num_listeners + priv->dev->unsent_unbind_msg_count >
	     CONFIG_KBUS_MAX_UNSENT_UNBIND_MESSAGES)) {
		struct kbus_msg_id in_reply_to = { 0, 0 };	/* no-one */
		/*
		 * Either the list had already gone tragic, or we've
		 * filled it up with "normal" unbind event messages
		 */
		priv->dev->unsent_unbind_is_tragic = true;

		/* In which case we need a different message */
		kbus_free_message(msg);
		msg = kbus_build_kbus_message(priv->dev,
					      KBUS_MSG_NAME_UNBIND_EVENTS_LOST,
					      0, 0, in_reply_to);
		if (msg == NULL)
			goto done_sending;

		for (ii = 0; ii < num_listeners; ii++) {
			/*
			 * We only want to add a "gone tragic" message if the
			 * recipient does not already have such a message
			 * stacked...
			 */
			if (kbus_listener_already_got_tragic_msg(priv->dev,
						 listeners[ii]->bound_to))
				continue;
			retval = kbus_remember_unsent_unbind_event(priv->dev,
					   listeners[ii]->bound_to,
					   msg, listeners[ii]);
			/* And remember that we've got something on the
			 * set-aside list */
			listeners[ii]->bound_to->maybe_got_unsent_unbind_msgs =
			    true;
			if (retval)
				break;	/* No good choice here */
		}
	} else {
		/* There's room to add these messages as-is */
		for (ii = 0; ii < num_listeners; ii++) {
			retval = kbus_remember_unsent_unbind_event(priv->dev,
					   listeners[ii]->bound_to,
					   msg, listeners[ii]);
			/* And remember that we've got something on the
			 * set-aside list */
			listeners[ii]->bound_to->maybe_got_unsent_unbind_msgs =
			    true;
			if (retval)
				break;	/* No good choice here */
		}
	}

done_sending:
	kfree(listeners);
	/* Don't forget to free our copy of the message */
	if (msg)
		kbus_free_message(msg);
	/* We aren't returning any status code. Oh well. */
}

/*
 * Return how many messages we have in the unsent Replier Unbind Event list.
 */
static u32 kbus_count_unsent_unbind_msgs(struct kbus_private_data *priv)
{
	struct kbus_dev *dev = priv->dev;

	struct kbus_unsent_message_item *ptr;
	struct kbus_unsent_message_item *next;

	u32 count = 0;

	kbus_maybe_dbg(dev, "%u/%u Counting unsent unbind messages\n",
		       dev->index, priv->id);

	list_for_each_entry_safe(ptr, next, &dev->unsent_unbind_msg_list,
				 list) {
		if (ptr->send_to_id == priv->id)
			count++;
	}
	return count;
}

/*
 * Maybe move an unsent Replier Unbind Event message to the main message list.
 *
 * Check if we have an unsent event on the set-aside list. If we do, move the
 * first one across to our normal message queue.
 *
 * Returns 0 if all goes well, or a negative value if something went wrong.
 */
static int kbus_maybe_move_unsent_unbind_msg(struct kbus_private_data *priv)
{
	struct kbus_dev *dev = priv->dev;

	struct kbus_unsent_message_item *ptr;
	struct kbus_unsent_message_item *next;

	kbus_maybe_dbg(dev,
		       "%u/%u Looking for an unsent unbind message\n",
		       dev->index, priv->id);

	list_for_each_entry_safe(ptr, next, &dev->unsent_unbind_msg_list,
				 list) {
		int retval;

		if (ptr->send_to_id != priv->id)
			continue;

		kbus_maybe_report_message(priv->dev, ptr->msg);
		/*
		 * Move the message into our normal message queue.
		 *
		 * We *must* use kbus_push_message() to do this, as
		 * we wish to keep our promise that this shall be the
		 * only way of adding a message to the queue.
		 */
		retval = kbus_push_message(priv, ptr->msg, ptr->binding, false);
		if (retval)
			return retval;	/* What else can we do? */

		/* Remove it from the list */
		list_del(&ptr->list);
		/* Mustn't forget to free *our* copy of the message */
		kbus_free_message(ptr->msg);
		kfree(ptr);
		dev->unsent_unbind_msg_count--;
		goto check_tragic;
	}

	/*
	 * Since we didn't find anything, we can safely unset the flag that
	 * says there might be something to find...
	 */
	priv->maybe_got_unsent_unbind_msgs = false;

check_tragic:
	/*
	 * And if we've succeeded in emptying the list, we can unset the
	 * "gone tragic" flag for it, too, if it was set.
	 */
	if (list_empty(&dev->unsent_unbind_msg_list))
		dev->unsent_unbind_is_tragic = false;
	return 0;
}

/*
 * Forget any outstanding unsent Replier Unbind Event messages for this binding.
 *
 * Called from kbus_release.
 */
static void kbus_forget_unbound_unsent_unbind_msgs(
					struct kbus_private_data *priv,
					struct kbus_message_binding *binding)
{
	struct kbus_dev *dev = priv->dev;

	struct kbus_unsent_message_item *ptr;
	struct kbus_unsent_message_item *next;

	u32 count = 0;

	kbus_maybe_dbg(dev,
		       " %u/%u Forgetting unsent unbind messages for "
		       "this binding\n",
		       dev->index, priv->id);

	list_for_each_entry_safe(ptr, next, &dev->unsent_unbind_msg_list,
			list) {
		if (ptr->binding == binding) {
			/* Remove it from the list */
			list_del(&ptr->list);
			/* And forget all about it... */
			kbus_free_message(ptr->msg);
			kfree(ptr);
			dev->unsent_unbind_msg_count--;
			count++;
		}
	}
	kbus_maybe_dbg(dev, "%u/%u Forgot %u unsent unbind messages\n",
		       dev->index, priv->id, count);
	/*
	 * And if we've succeeded in emptying the list, we can unset the
	 * "gone tragic" flag for it, too, if it was set.
	 */
	if (list_empty(&dev->unsent_unbind_msg_list))
		dev->unsent_unbind_is_tragic = false;
}

/*
 * Forget any outstanding unsent Replier Unbind Event messages for this Replier.
 *
 * Called from kbus_release.
 */
static void kbus_forget_my_unsent_unbind_msgs(struct kbus_private_data *priv)
{
	struct kbus_dev *dev = priv->dev;

	struct kbus_unsent_message_item *ptr;
	struct kbus_unsent_message_item *next;

	u32 count = 0;

	kbus_maybe_dbg(dev,
		       "%u/%u Forgetting my unsent unbind messages\n",
		       dev->index, priv->id);

	list_for_each_entry_safe(ptr, next, &dev->unsent_unbind_msg_list,
			list) {
		if (ptr->send_to_id == priv->id) {
			/* Remove it from the list */
			list_del(&ptr->list);
			/* And forget all about it... */
			kbus_free_message(ptr->msg);
			kfree(ptr);
			dev->unsent_unbind_msg_count--;
			count++;
		}
	}
	kbus_maybe_dbg(dev, "%u/%u Forgot %u unsent unbind messages\n",
		       dev->index, priv->id, count);
	/*
	 * And if we've succeeded in emptying the list, we can unset the
	 * "gone tragic" flag for it, too, if it was set.
	 */
	if (list_empty(&dev->unsent_unbind_msg_list))
		dev->unsent_unbind_is_tragic = false;
}

/*
 * Forget any outstanding unsent Replier Unbind Event messages.
 *
 * Assumed to be called because the device is closing, and thus doesn't lock,
 * or worry about lost messages.
 */
static void kbus_forget_unsent_unbind_msgs(struct kbus_dev *dev)
{
	struct kbus_unsent_message_item *ptr;
	struct kbus_unsent_message_item *next;

	kbus_maybe_dbg(dev,
		       "  %u Forgetting unsent unbind event messages\n",
		       dev->index);

	list_for_each_entry_safe(ptr, next, &dev->unsent_unbind_msg_list,
			list) {

		if (!kbus_message_name_matches(
					    ptr->msg->name_ref->name,
					    ptr->msg->name_len,
					    KBUS_MSG_NAME_REPLIER_BIND_EVENT))
			kbus_maybe_report_message(dev, ptr->msg);

		/* Remove it from the list */
		list_del(&ptr->list);
		/* And forget all about it... */
		kbus_free_message(ptr->msg);
		kfree(ptr);
		dev->unsent_unbind_msg_count--;
	}
}

/*
 * Remove all bindings for a particular listener.
 *
 * Called from kbus_release, which will itself handle removing messages
 * (that *were* bound) from the message queue.
 */
static void kbus_forget_my_bindings(struct kbus_private_data *priv)
{
	struct kbus_dev *dev = priv->dev;
	u32 bound_to_id = priv->id;

	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	kbus_maybe_dbg(dev, "%u/%u Forgetting my bindings\n",
		       dev->index, priv->id);

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		if (bound_to_id != ptr->bound_to_id)
			continue;

		kbus_maybe_dbg(dev, "  Unbound %u %c '%.*s'\n",
			       ptr->bound_to_id, (ptr->is_replier ? 'R' : 'L'),
			       ptr->name_len, ptr->name);

		if (ptr->is_replier && dev->report_replier_binds)
			kbus_safe_report_unbinding(priv, ptr->name_len,
							 ptr->name);

		list_del(&ptr->list);
		kfree(ptr->name);
		kfree(ptr);
	}
}

/*
 * Remove all bindings.
 *
 * Assumed to be called because the device is closing, and thus doesn't lock,
 * nor does it worry about generating synthetic messages as requests are doomed
 * not to get replies.
 */
static void kbus_forget_all_bindings(struct kbus_dev *dev)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	kbus_maybe_dbg(dev, "%u Forgetting bindings\n", dev->index);

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {

		kbus_maybe_dbg(dev, "  Unbinding %u %c '%.*s'\n",
			       ptr->bound_to_id,
			       (ptr->is_replier ? 'R' : 'L'),
			       ptr->name_len, ptr->name);

		/* And we don't want anyone reading for this */
		list_del(&ptr->list);
		kfree(ptr->name);
		kfree(ptr);
	}
}

/*
 * Add a new open file to our remembrances.
 *
 * Returns 0 if all went well, a negative value if it did not.
 */
static int kbus_remember_open_ksock(struct kbus_dev *dev,
				    struct kbus_private_data *priv)
{
	list_add(&priv->list, &dev->open_ksock_list);

	kbus_maybe_dbg(priv->dev, "Remembered 'open file' id %u\n",
		       priv->id);
	return 0;
}

/*
 * Retrieve the pointer to an open file's data
 *
 * Return NULL if we can't find it.
 */
static struct kbus_private_data *kbus_find_open_ksock(struct kbus_dev *dev,
						      u32 id)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	list_for_each_entry_safe(ptr, next, &dev->open_ksock_list, list) {
		if (id == ptr->id) {
			kbus_maybe_dbg(dev, "  %u Found open Ksock %u\n",
				       dev->index, id);
			return ptr;
		}
	}
	kbus_maybe_dbg(dev, "  %u Could not find open Ksock %u\n",
		       dev->index, id);
	return NULL;
}

/*
 * Remove an open file remembrance.
 *
 * Returns 0 if all went well, -EINVAL if we couldn't find the open Ksock
 */
static int kbus_forget_open_ksock(struct kbus_dev *dev, u32 id)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	/* We don't want anyone writing to the list whilst we do this */
	list_for_each_entry_safe(ptr, next, &dev->open_ksock_list, list) {
		if (id != ptr->id)
			continue;

		kbus_maybe_dbg(dev, "  %u Forgetting open Ksock %u\n",
			       dev->index, id);

		/* So remove it from our list */
		list_del(&ptr->list);
		/* But *we* mustn't free the actual datastructure! */
		return 0;
	}
	kbus_maybe_dbg(dev, "  %u Could not forget open Ksock %u\n",
		       dev->index, id);

	return -EINVAL;
}

/*
 * Forget all our "open file" remembrances.
 *
 * Assumed to be called because the device is closing, and thus doesn't lock.
 */
static void kbus_forget_all_open_ksocks(struct kbus_dev *dev)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	list_for_each_entry_safe(ptr, next, &dev->open_ksock_list, list) {

		kbus_maybe_dbg(dev, "  %u Forgetting open Ksock %u\n",
			       dev->index, ptr->id);

		/* So remove it from our list */
		list_del(&ptr->list);
		/* But *we* mustn't free the actual datastructure! */
	}
}

static int kbus_open(struct inode *inode, struct file *filp)
{
	struct kbus_private_data *priv;
	struct kbus_dev *dev;

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/*
	 * Use the official magic to retrieve our actual device data
	 * so we can remember it for other file operations.
	 */
	dev = container_of(inode->i_cdev, struct kbus_dev, cdev);

	if (mutex_lock_interruptible(&dev->mux)) {
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
	if (dev->next_ksock_id == 0)
		dev->next_ksock_id++;

	memset(priv, 0, sizeof(*priv));
	priv->dev = dev;
	priv->id = dev->next_ksock_id++;
	priv->pid = current->pid;
	priv->max_messages = CONFIG_KBUS_DEF_MAX_MESSAGES;
	priv->sending = false;
	priv->num_replies_unsent = 0;
	priv->max_replies_unsent = 0;

	if (kbus_init_msg_id_memory(priv)) {
		kbus_empty_read_msg(priv);
		kfree(priv);
		return -EFAULT;
	}
	INIT_LIST_HEAD(&priv->message_queue);
	INIT_LIST_HEAD(&priv->replies_unsent);

	init_waitqueue_head(&priv->read_wait);

	/* Note that we immediately have a space available for a message */
	wake_up_interruptible(&dev->write_wait);

	(void)kbus_remember_open_ksock(dev, priv);

	filp->private_data = priv;

	mutex_unlock(&dev->mux);

	kbus_maybe_dbg(dev, "%u/%u OPEN\n", dev->index, priv->id);

	return 0;
}

static int kbus_release(struct inode *inode __always_unused, struct file *filp)
{
	int retval2 = 0;
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;

	if (mutex_lock_interruptible(&dev->mux))
		return -ERESTARTSYS;

	kbus_maybe_dbg(dev, "%u/%u RELEASE\n", dev->index, priv->id);

	kbus_empty_read_msg(priv);
	kbus_empty_write_msg(priv);

	kbus_empty_msg_id_memory(priv);

	kbus_empty_message_queue(priv);
	kbus_forget_my_bindings(priv);
	if (priv->maybe_got_unsent_unbind_msgs)
		kbus_forget_my_unsent_unbind_msgs(priv);
	kbus_empty_replies_unsent(priv);
	retval2 = kbus_forget_open_ksock(dev, priv->id);
	kfree(priv);

	mutex_unlock(&dev->mux);

	return retval2;
}

/*
 * Determine the private data for the given listener/replier id.
 *
 * Return NULL if we can't find it.
 */
static struct kbus_private_data
*kbus_find_private_data(struct kbus_private_data *our_priv,
			struct kbus_dev *dev, u32 id)
{
	struct kbus_private_data *l_priv;
	if (id == our_priv->id) {
		/* Heh, it's us, we know who we are! */
		kbus_maybe_dbg(dev, "  -- Id %u is us\n", id);

		l_priv = our_priv;
	} else {
		/* OK, look it up */
		kbus_maybe_dbg(dev, "  -- Looking up id %u\n", id);

		l_priv = kbus_find_open_ksock(dev, id);
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
static int kbus_queue_is_full(struct kbus_private_data *priv,
			      char *what __maybe_unused, int is_reply)
{
	/*
	 * When figuring out how "full" the message queue is, we need
	 * to take account of the messages already in the queue (!),
	 * and also the replies that still need to be written to the
	 * queue.
	 *
	 * Of course, if we're checking because we want to send one
	 * of the Replies that we are keeping room for, we need to
	 * remember to account for that!
	 */
	int already_accounted_for = priv->message_count +
	    priv->outstanding_requests.count;

	if (is_reply)
		already_accounted_for--;

	kbus_maybe_dbg(priv->dev,
		       "  %u/%u Message queue: count %d + "
		       "outstanding %d %s= %d, max %d\n",
		       priv->dev->index, priv->id, priv->message_count,
		       priv->outstanding_requests.count,
		       (is_reply ? "-1 " : ""), already_accounted_for,
		       priv->max_messages);

	if (already_accounted_for < priv->max_messages) {
		return false;
	} else {
		kbus_maybe_dbg(priv->dev,
			       "  Message queue for %s %u is full"
			       " (%u+%u%s > %u messages)\n", what, priv->id,
			       priv->message_count,
			       priv->outstanding_requests.count,
			       (is_reply ? "-1" : ""), priv->max_messages);
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
 * If the message is a Reply, and the is sender is no longer connected (it has
 * released its Ksock), then we return -EADDRNOTAVAIL.
 *
 * If the message couldn't be sent because some of the targets (those that we
 * *have* to deliver to) had full queues, then it will return -EAGAIN or
 * -EBUSY. If -EAGAIN is returned, then the caller should try again later, if
 * -EBUSY then it should not.
 *
 * Otherwise, it returns a negative value for error.
 */
static int kbus_write_to_recipients(struct kbus_private_data *priv,
				    struct kbus_dev *dev,
				    struct kbus_msg *msg)
{
	struct kbus_message_binding **listeners = NULL;
	struct kbus_message_binding *replier = NULL;
	struct kbus_private_data *reply_to = NULL;
	ssize_t retval = 0;
	int num_listeners;
	int ii;
	int num_sent = 0;	/* # successfully "sent" */

	int all_or_fail = msg->flags & KBUS_BIT_ALL_OR_FAIL;
	int all_or_wait = msg->flags & KBUS_BIT_ALL_OR_WAIT;

	kbus_maybe_dbg(priv->dev, "  all_or_fail %d, all_or_wait %d\n",
		       all_or_fail, all_or_wait);

	/*
	 * Remember that
	 * (a) a listener may occur more than once in our array, and
	 * (b) we have 0 or 1 repliers, but
	 * (c) the replier is *not* one of the listeners.
	 */
	num_listeners = kbus_find_listeners(dev, &listeners, &replier,
					    msg->name_len, msg->name_ref->name);
	if (num_listeners < 0) {
		kbus_maybe_dbg(priv->dev,
			       "  Error %d finding listeners\n",
			       num_listeners);

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
		kbus_maybe_dbg(priv->dev,
			       "  Message wants a reply, "
			       "but no replier\n");
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
		kbus_maybe_dbg(priv->dev,
			       "  Considering sender-of-request %u\n",
			       msg->to);

		reply_to = kbus_find_private_data(priv, dev, msg->to);
		if (reply_to == NULL) {
			kbus_maybe_dbg(priv->dev,
				       "  Can't find sender-of-request"
				       " %u\n", msg->to);

			/* We can't find the original Sender */
			retval = -EADDRNOTAVAIL;
			goto done_sending;
		}

		/* Are they expecting this reply? */
		if (kbus_find_msg_id(reply_to, &msg->in_reply_to)) {
			/* No, so we aren't allowed to send it */
			retval = -ECONNREFUSED;
			goto done_sending;
		}

		if (kbus_queue_is_full(reply_to, "sender-of-request", true)) {
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
		kbus_maybe_dbg(priv->dev, "  Considering replier %u\n",
			       replier->bound_to_id);
		/*
		 * If the 'to' field was set, then we only want to send it if
		 * it is *that* specific replier (and otherwise we want to fail
		 * with "that's the wrong person for this (stateful) request").
		 */
		if (msg->to && (replier->bound_to_id != msg->to)) {

			kbus_maybe_dbg(priv->dev, "  ..Request to %u,"
				       " but replier is %u\n", msg->to,
				       replier->bound_to_id);

			retval = -EPIPE;	/* Well, sort of */
			goto done_sending;
		}

		if (kbus_queue_is_full(replier->bound_to, "replier", false)) {
			if (all_or_wait)
				retval = -EAGAIN;	/* try again later */
			else
				retval = -EBUSY;
			goto done_sending;
		}
	}

	for (ii = 0; ii < num_listeners; ii++) {

		kbus_maybe_dbg(priv->dev, "  Considering listener %u\n",
			       listeners[ii]->bound_to_id);

		if (kbus_queue_is_full
		    (listeners[ii]->bound_to, "listener", false)) {
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
		retval = kbus_push_message(reply_to, msg, NULL, true);
		if (retval == 0) {
			num_sent++;
			/*
			 * In which case, we *have* sent this reply,
			 * and can forget about needing to do so
			 * (there's not much we can do with an error
			 * in this, so just ignore it)
			 */
			(void)kbus_reply_now_sent(priv, &msg->in_reply_to);
		} else {
			goto done_sending;
		}
	}

	/* If it's a request, and we've got a replier for it, send it */
	if (replier) {
		retval =
		    kbus_push_message(replier->bound_to, msg, replier, true);
		if (retval)
			goto done_sending;

		num_sent++;
		/* And we'll need a reply for that, thank you */
		retval = kbus_remember_msg_id(priv, &msg->id);
		if (retval)
			/*
			 * Out of memory - what *can* we do?
			 * (basically, nothing, it's all gone horribly
			 * wrong)
			 */
			goto done_sending;
	}

	/* For each listener, if they're still interested, send it */
	for (ii = 0; ii < num_listeners; ii++) {
		struct kbus_message_binding *listener = listeners[ii];
		if (listener) {
			retval = kbus_push_message(listener->bound_to, msg,
						   listener, false);
			if (retval == 0)
				num_sent++;
			else
				goto done_sending;
		}
	}

	retval = 0;

done_sending:
	kfree(listeners);
	return retval;
}

/*
 * Handle moving over the next chunk of data bytes from the user.
 */
static int kbus_write_data_parts(struct kbus_private_data *priv,
				 const char __user *buf,
				 size_t buf_pos, size_t bytes_to_use)
{
	struct kbus_write_msg *this = &(priv->write);

	u32 num_parts = this->ref_data->num_parts;
	size_t local_count = bytes_to_use;
	size_t local_buf_pos = 0;

	while (local_count) {
		unsigned ii = this->ref_data_index;
		unsigned this_part_len;
		size_t sofar, needed, to_use;

		unsigned *lengths = this->ref_data->lengths;
		unsigned long *parts = this->ref_data->parts;

		if (ii == num_parts - 1)
			this_part_len = this->ref_data->last_page_len;
		else
			this_part_len = KBUS_PART_LEN;

		sofar = lengths[ii];

		needed = this_part_len - sofar;
		to_use = min(needed, local_count);

		if (copy_from_user((char *)parts[ii] + sofar,
				   buf + buf_pos + local_buf_pos, to_use)) {
			dev_err(priv->dev->dev, "copy from data failed"
			       " (part %d: %u of %u to %p + %u)\n",
			       this->ref_data_index,
			       (unsigned)to_use, (unsigned)local_count,
			       (void *)parts[ii], (unsigned)sofar);
			return -EFAULT;
		}

		lengths[ii] += to_use;
		local_count -= to_use;
		local_buf_pos += to_use;

		if (lengths[ii] == this_part_len) {
			/* This part is full */
			this->ref_data_index++;
		}
	}
	return 0;
}

/*
 * Handle moving over the next chunk of bytes from the user to our message.
 *
 * 'buf' is the buffer of data the user gave us.
 *
 * 'buf_pos' is the offset in that buffer from which we are to take bytes.
 * We alter that by how many bytes we do take.
 *
 * 'count' is the number of bytes we're still to take from 'buf'. We also
 * alter 'count' by how many bytes we do take (downwards).
 */
static int kbus_write_parts(struct kbus_private_data *priv,
			    const char __user *buf,
			    size_t *buf_pos, size_t *count)
{
	struct kbus_write_msg *this = &(priv->write);
	ssize_t retval = 0;

	size_t bytes_needed;	/* ...to fill the current part */
	size_t bytes_to_use;	/* ...from the user's data */

	struct kbus_msg *msg = this->msg;
	struct kbus_message_header *user_msg =
	    (struct kbus_message_header *)&this->user_msg;

	if (this->is_finished) {
		dev_err(priv->dev->dev, "pid %u [%s]"
		       " Attempt to write data after the end guard in a"
		       " message (%u extra byte%s) - did you forget to"
		       " 'send'?\n",
		       current->pid, current->comm,
		       (unsigned)*count, *count == 1 ? "" : "s");
		return -EMSGSIZE;
	}

	switch (this->which) {

	case KBUS_PART_HDR:
		bytes_needed = sizeof(*user_msg) - this->pos;
		bytes_to_use = min(bytes_needed, *count);

		if (copy_from_user((char *)user_msg + this->pos,
				   buf + *buf_pos, bytes_to_use)) {
			dev_err(priv->dev->dev,
			       "copy from user failed (msg hdr: "
			       "%u of %u to %p + %u)\n",
			       (unsigned)bytes_to_use, (unsigned)*count, msg,
			       this->pos);
			return -EFAULT;
		}
		if (bytes_needed == bytes_to_use) {
			/*
			 * At this point, we can check the message header makes
			 * sense
			 */
			retval = kbus_check_message_written(priv->dev, this);
			if (retval)
				return retval;

			msg->id = user_msg->id;
			msg->in_reply_to = user_msg->in_reply_to;
			msg->to = user_msg->to;
			msg->from = user_msg->from;
			msg->orig_from = user_msg->orig_from;
			msg->final_to = user_msg->final_to;
			msg->extra = user_msg->extra;
			msg->flags = user_msg->flags;
			msg->name_len = user_msg->name_len;
			msg->data_len = user_msg->data_len;
			/* Leaving msg->name|data_ref still unset */

			this->user_name_ptr = user_msg->name;
			this->user_data_ptr = user_msg->data;

			if (user_msg->name)
				/*
				 * If we're reading a "pointy" message header,
				 * then that's all we need - we shan't try to
				 * copy the message name and any data until the
				 * user says to SEND.
				 */
				this->is_finished = true;
			else
				this->pointers_are_local = true;
		}
		break;

	case KBUS_PART_NAME:
		if (this->ref_name == NULL) {
			char *name = kmalloc(msg->name_len + 1, GFP_KERNEL);
			if (!name) {
				dev_err(priv->dev->dev,
					"Cannot kmalloc message name\n");
				return -ENOMEM;
			}
			name[msg->name_len] = 0;	/* always */
			name[0] = 0;	/* we don't know the name yet */
			this->ref_name = kbus_wrap_name_in_ref(name);
			if (!this->ref_name) {
				kfree(name);
				dev_err(priv->dev->dev,
					"Cannot kmalloc ref to message name\n");
				return -ENOMEM;
			}
		}
		bytes_needed = msg->name_len - this->pos;
		bytes_to_use = min(bytes_needed, *count);

		if (copy_from_user(this->ref_name->name + this->pos,
				   buf + *buf_pos, bytes_to_use)) {
			dev_err(priv->dev->dev, "copy from user failed"
			       " (name: %d of %d to %p + %u)\n",
			       (unsigned)bytes_to_use, (unsigned)*count,
			       this->ref_name->name, this->pos);
			return -EFAULT;
		}
		if (bytes_needed == bytes_to_use) {
			/*
			 * We can check the name now it is in kernel space - we
			 * want to do this before we sort out the data, since
			 * that can involve a *lot* of copying...
			 */
			if (kbus_invalid_message_name(priv->dev,
						      this->ref_name->name,
						      msg->name_len))
				return -EBADMSG;

			this->msg->name_ref = this->ref_name;
			this->ref_name = NULL;
		}
		break;

	case KBUS_PART_NPAD:
		bytes_needed = KBUS_PADDED_NAME_LEN(msg->name_len) -
		    msg->name_len - this->pos;
		bytes_to_use = min(bytes_needed, *count);
		break;

	case KBUS_PART_DATA:
		if (msg->data_len == 0) {
			bytes_needed = 0;
			bytes_to_use = 0;
			break;
		}
		if (this->ref_data == NULL) {
			if (kbus_alloc_ref_data(priv, msg->data_len,
						&this->ref_data))
				return -ENOMEM;
			this->ref_data_index = 0;	/* current part index */
		}
		/* Overall, how far are we through the message's data? */
		bytes_needed = msg->data_len - this->pos;
		bytes_to_use = min(bytes_needed, *count);
		/* So let's add 'bytes_to_use' bytes to our message data */
		retval = kbus_write_data_parts(priv, buf, *buf_pos,
					       bytes_to_use);
		if (retval) {
			kbus_lower_data_ref(this->ref_data);
			this->ref_data = NULL;
			return retval;
		}
		if (bytes_needed == bytes_to_use) {
			/* Hooray - we've finished our data */
			this->msg->data_ref = this->ref_data;
			this->ref_data = NULL;
		}
		break;

	case KBUS_PART_DPAD:
		bytes_needed = KBUS_PADDED_DATA_LEN(msg->data_len) -
		    msg->data_len - this->pos;
		bytes_to_use = min(bytes_needed, *count);
		break;

	case KBUS_PART_FINAL_GUARD:
		bytes_needed = 4 - this->pos;
		bytes_to_use = min(bytes_needed, *count);
		if (copy_from_user((char *)(&this->guard) + this->pos,
				   buf + *buf_pos, bytes_to_use)) {
			dev_err(priv->dev->dev, "copy from user failed"
			       " (final guard: %u of %u to %p + %u)\n",
			       (unsigned)bytes_to_use, (unsigned)*count,
			       &this->guard, this->pos);
			return -EFAULT;
		}
		if (bytes_needed == bytes_to_use) {
			if (this->guard != KBUS_MSG_END_GUARD) {
				dev_err(priv->dev->dev, "pid %u [%s]"
				       " (entire) message end guard is "
				       "%08x, not %08x\n",
				       current->pid, current->comm,
				       this->guard, KBUS_MSG_END_GUARD);
				return -EINVAL;
			}
			this->is_finished = true;
		}
		break;

	default:
		dev_err(priv->dev->dev, "Internal error in write: unexpected"
		       " message part %d\n", this->which);
		return -EFAULT;	/* what *should* it be? */
	}

	*count -= bytes_to_use;
	*buf_pos += bytes_to_use;

	if (bytes_needed == bytes_to_use) {
		this->which++;
		this->pos = 0;
	} else {
		this->pos += bytes_to_use;
	}
	return 0;
}

static ssize_t kbus_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *f_pos __maybe_unused)
{
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;
	ssize_t retval = 0;
	size_t bytes_left = count;
	size_t buf_pos = 0;

	struct kbus_write_msg *this = &priv->write;

	if (mutex_lock_interruptible(&dev->mux))
		return -EAGAIN;

	kbus_maybe_dbg(priv->dev, "%u/%u WRITE count %u, pos %d\n",
		       dev->index, priv->id, (unsigned)count, (int)*f_pos);

	/*
	 * If we've already started to try sending a message, we don't
	 * want to continue appending to it
	 */
	if (priv->sending) {
		retval = -EALREADY;
		goto done;
	}

	if (this->msg == NULL) {
		/* Clearly, the start of a new message */
		memset(this, 0, sizeof(*this));

		/* This is the new (internal) message we're preparing */
		this->msg = kmalloc(sizeof(*(this->msg)), GFP_KERNEL);
		if (!this->msg) {
			retval = -ENOMEM;
			goto done;
		}
		memset(this->msg, 0, sizeof(*(this->msg)));
	}

	while (bytes_left) {
		retval = kbus_write_parts(priv, buf, &buf_pos, &bytes_left);
		if (retval)
			goto done;
	}

done:
	kbus_maybe_dbg(priv->dev, "%u/%u WRITE ends with retval %d\n",
		       dev->index, priv->id, (int)retval);

	if (retval)
		kbus_empty_write_msg(priv);
	mutex_unlock(&dev->mux);
	if (retval)
		return retval;
	else
		return count;
}

static ssize_t kbus_read(struct file *filp, char __user *buf, size_t count,
			 loff_t *f_pos __maybe_unused)
{
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;
	struct kbus_read_msg *this = &(priv->read);
	ssize_t retval = 0;
	u32 len, left;
	u32 which = this->which;

	if (mutex_lock_interruptible(&dev->mux))
		return -EAGAIN;	/* Just try again later */

	kbus_maybe_dbg(priv->dev, "%u/%u READ count %u, pos %d\n",
		       dev->index, priv->id, (unsigned)count, (int)*f_pos);

	if (this->msg == NULL) {
		/* No message to read at the moment */
		kbus_maybe_dbg(priv->dev, "  Nothing to read\n");
		retval = 0;
		goto done;
	}

	/*
	 * Read each of the parts of a message until we're read 'count'
	 * characters, or run off the end of the message.
	 */
	while (which < KBUS_NUM_PARTS && count > 0) {
		if (this->lengths[which] == 0) {
			kbus_maybe_dbg(priv->dev,
				       "  xx which %d, read_len[%d] %u\n",
				       which, which, this->lengths[which]);
			this->pos = 0;
			which++;
			continue;
		}

		if (which == KBUS_PART_DATA) {
			struct kbus_data_ptr *dp = this->msg->data_ref;

			left = dp->lengths[this->ref_data_index] - this->pos;
			len = min(left, (u32) count);
			if (len) {
				if (copy_to_user(buf,
						 (void *)
						 dp->parts[this->ref_data_index]
							 + this->pos, len)) {
					dev_err(priv->dev->dev,
					       "error reading from dev %u/%u\n",
					       dev->index, priv->id);
					retval = -EFAULT;
					goto done;
				}
				buf += len;
				retval += len;
				count -= len;
				this->pos += len;
			}

			if (this->pos == dp->lengths[this->ref_data_index]) {
				this->pos = 0;
				this->ref_data_index++;
			}
			if (this->ref_data_index == dp->num_parts) {
				this->pos = 0;
				which++;
			}
		} else {
			left = this->lengths[which] - this->pos;
			len = min(left, (u32) count);
			if (len) {
				if (copy_to_user(buf,
						 this->parts[which] + this->pos,
						 len)) {
					dev_err(priv->dev->dev,
					       "error reading from dev %u/%u\n",
					       dev->index, priv->id);
					retval = -EFAULT;
					goto done;
				}
				buf += len;
				retval += len;
				count -= len;
				this->pos += len;
			}

			if (this->pos == this->lengths[which]) {
				this->pos = 0;
				which++;
			}
		}
	}

	if (which < KBUS_NUM_PARTS)
		this->which = which;
	else
		kbus_empty_read_msg(priv);

done:
	mutex_unlock(&dev->mux);
	return retval;
}

static int kbus_bind(struct kbus_private_data *priv,
		     struct kbus_dev *dev, unsigned long arg)
{
	int retval = 0;
	struct kbus_bind_request *bind;
	char *name = NULL;

	bind = kmalloc(sizeof(*bind), GFP_KERNEL);
	if (!bind)
		return -ENOMEM;
	if (copy_from_user(bind, (void __user *)arg, sizeof(*bind))) {
		retval = -EFAULT;
		goto done;
	}

	if (bind->name_len == 0) {
		kbus_maybe_dbg(dev, "bind name is length 0\n");
		retval = -EBADMSG;
		goto done;
	} else if (bind->name_len > KBUS_MAX_NAME_LEN) {
		kbus_maybe_dbg(dev, "bind name is length %d\n",
			       bind->name_len);
		retval = -ENAMETOOLONG;
		goto done;
	}

	name = kmalloc(bind->name_len + 1, GFP_KERNEL);
	if (!name) {
		retval = -ENOMEM;
		goto done;
	}
	if (copy_from_user(name, (char __user *) bind->name, bind->name_len)) {
		retval = -EFAULT;
		goto done;
	}
	name[bind->name_len] = 0;

	if (kbus_bad_message_name(name, bind->name_len)) {
		retval = -EBADMSG;
		goto done;
	}

	if (bind->is_replier && !strcmp(name,
					KBUS_MSG_NAME_REPLIER_BIND_EVENT)) {
		kbus_maybe_dbg(priv->dev, "cannot bind %s as a Replier\n",
			       KBUS_MSG_NAME_REPLIER_BIND_EVENT);
		retval = -EBADMSG;
		goto done;
	}

	kbus_maybe_dbg(priv->dev, "%u/%u BIND %c '%.*s'\n",
		       priv->dev->index, priv->id,
		       (bind->is_replier ? 'R' : 'L'), bind->name_len, name);

	retval = kbus_remember_binding(dev, priv,
				       bind->is_replier, bind->name_len, name);
	if (retval == 0)
		/* The binding will use our copy of the message name */
		name = NULL;

done:
	kfree(name);
	kfree(bind);
	return retval;
}

static int kbus_unbind(struct kbus_private_data *priv,
		       struct kbus_dev *dev, unsigned long arg)
{
	int retval = 0;
	struct kbus_bind_request *bind;
	char *name = NULL;
	u32 old_message_count = priv->message_count;

	bind = kmalloc(sizeof(*bind), GFP_KERNEL);
	if (!bind)
		return -ENOMEM;
	if (copy_from_user(bind, (void __user *)arg, sizeof(*bind))) {
		retval = -EFAULT;
		goto done;
	}

	if (bind->name_len == 0) {
		kbus_maybe_dbg(priv->dev, "unbind name is length 0\n");
		retval = -EBADMSG;
		goto done;
	} else if (bind->name_len > KBUS_MAX_NAME_LEN) {
		kbus_maybe_dbg(priv->dev, "unbind name is length %d\n",
			       bind->name_len);
		retval = -ENAMETOOLONG;
		goto done;
	}

	name = kmalloc(bind->name_len + 1, GFP_KERNEL);
	if (!name) {
		retval = -ENOMEM;
		goto done;
	}
	if (copy_from_user(name, (char __user *) bind->name, bind->name_len)) {
		retval = -EFAULT;
		goto done;
	}
	name[bind->name_len] = 0;

	if (kbus_bad_message_name(name, bind->name_len)) {
		retval = -EBADMSG;
		goto done;
	}

	kbus_maybe_dbg(priv->dev, "%u/%u UNBIND %c '%.*s'\n",
		       priv->dev->index, priv->id,
		       (bind->is_replier ? 'R' : 'L'), bind->name_len, name);

	retval = kbus_forget_binding(dev, priv,
				     bind->is_replier, bind->name_len, name);

	/*
	 * If we're unbinding from $.KBUS.ReplierBindEvent, and there
	 * are (or may be) any such kept for us on the unread Replier
	 * Unbind Event list, then we need to remove them as well...
	 *
	 * NOTE that the following only checks for exact matchs to
	 * $.KBUS.ReplierBindEvent, which should be sufficient...
	 */
	if (priv->maybe_got_unsent_unbind_msgs &&
	    !strcmp(name, KBUS_MSG_NAME_REPLIER_BIND_EVENT))
		kbus_forget_my_unsent_unbind_msgs(priv);

	/*
	 * If that removed any messages from the message queue, then we have
	 * room to consider moving a message across from the unread Replier
	 * Unbind Event list
	 */
	if (priv->message_count < old_message_count &&
	    priv->maybe_got_unsent_unbind_msgs) {
		int rv = kbus_maybe_move_unsent_unbind_msg(priv);
		/* If this fails, we're probably stumped */
		if (rv)
			/* The best we can do is grumble gently. We still
			 * want to return retval, not rv.
			 */
			dev_err(priv->dev->dev,
			       "Failed to move unsent messages on "
			       "unbind (error %d)\n", -rv);
	}

done:
	kfree(name);
	kfree(bind);
	return retval;
}

static int kbus_replier(struct kbus_private_data *priv __maybe_unused,
			struct kbus_dev *dev, unsigned long arg)
{
	struct kbus_private_data *replier;
	struct kbus_bind_query *query;
	char *name = NULL;
	int retval = 0;

	query = kmalloc(sizeof(*query), GFP_KERNEL);
	if (!query)
		return -ENOMEM;
	if (copy_from_user(query, (void __user *)arg, sizeof(*query))) {
		retval = -EFAULT;
		goto done;
	}

	if (query->name_len == 0 || query->name_len > KBUS_MAX_NAME_LEN) {
		kbus_maybe_dbg(priv->dev, "Replier name is length %d\n",
			       query->name_len);
		retval = -ENAMETOOLONG;
		goto done;
	}

	name = kmalloc(query->name_len + 1, GFP_KERNEL);
	if (!name) {
		retval = -ENOMEM;
		goto done;
	}
	if (copy_from_user(name, (char __user *) query->name,
						 query->name_len)) {
		retval = -EFAULT;
		goto done;
	}
	name[query->name_len] = 0;

	kbus_maybe_dbg(priv->dev, "%u/%u REPLIER for '%.*s'\n",
		       priv->dev->index, priv->id, query->name_len, name);

	retval = kbus_find_replier(dev, &replier, query->name_len, name);
	if (retval < 0)
		goto done;

	if (retval)
		query->return_id = replier->id;
	else
		query->return_id = 0;
	/*
	 * Copy the whole structure back, rather than try to work out (in a
	 * guaranteed-safe manner) where the 'id' actually lives
	 */
	if (copy_to_user((void __user *)arg, query, sizeof(*query))) {
		retval = -EFAULT;
		goto done;
	}
done:
	kfree(name);
	kfree(query);
	return retval;
}

/*
 * Make the next message ready for reading by the user.
 *
 * Returns 0 if there is no next message, 1 if there is, and a negative value
 * if there's an error.
 */
static int kbus_nextmsg(struct kbus_private_data *priv,
			unsigned long arg)
{
	int retval = 0;
	struct kbus_msg *msg;
	struct kbus_read_msg *this = &(priv->read);
	struct kbus_message_header *user_msg;

	kbus_maybe_dbg(priv->dev, "%u/%u NEXTMSG\n", priv->dev->index,
		       priv->id);

	/* If we were partway through a message, lose it */
	if (this->msg) {
		kbus_maybe_dbg(priv->dev, "  Dropping partial message\n");
		kbus_empty_read_msg(priv);
	}

	/* Have we got a next message? */
	msg = kbus_pop_message(priv);
	if (msg == NULL) {
		kbus_maybe_dbg(priv->dev, "  No next message\n");
		/*
		 * A return value of 0 means no message, and that's
		 * what __put_user returns for success.
		 */
		return __put_user(0, (u32 __user *) arg);
	}

	user_msg = (struct kbus_message_header *)&this->user_hdr;
	user_msg->start_guard = KBUS_MSG_START_GUARD;
	user_msg->id = msg->id;
	user_msg->in_reply_to = msg->in_reply_to;
	user_msg->to = msg->to;
	user_msg->from = msg->from;
	user_msg->orig_from = msg->orig_from;
	user_msg->final_to = msg->final_to;
	user_msg->extra = msg->extra;
	user_msg->flags = msg->flags;
	user_msg->name_len = msg->name_len;
	user_msg->data_len = msg->data_len;
	user_msg->name = NULL;
	user_msg->data = NULL;
	user_msg->end_guard = KBUS_MSG_END_GUARD;

	this->msg = msg;	/* Remember it so we can free it later */

	this->parts[KBUS_PART_HDR] = (char *)user_msg;
	this->parts[KBUS_PART_NAME] = msg->name_ref->name;
	/* direct to the string */

	this->parts[KBUS_PART_NPAD] = static_zero_padding;

	/* The data is treated specially - see kbus_read() */
	this->parts[KBUS_PART_DATA] = (char *)msg->data_ref;

	this->parts[KBUS_PART_DPAD] = static_zero_padding;
	this->parts[KBUS_PART_FINAL_GUARD] = (char *)&static_end_guard;

	this->lengths[KBUS_PART_HDR] = sizeof(*user_msg);
	this->lengths[KBUS_PART_NAME] = msg->name_len;
	this->lengths[KBUS_PART_NPAD] =
	    KBUS_PADDED_NAME_LEN(msg->name_len) - msg->name_len;

	/* The data is treated specially - see kbus_read() */
	this->lengths[KBUS_PART_DATA] = msg->data_len;
	this->lengths[KBUS_PART_DPAD] =
	    KBUS_PADDED_DATA_LEN(msg->data_len) - msg->data_len;

	this->lengths[KBUS_PART_FINAL_GUARD] = 4;

	/* And we'll be starting by writing out the first thing first */
	this->which = 0;
	this->pos = 0;
	this->ref_data_index = 0;

	/*
	 * If the message is a request (to us), then this is the approriate
	 * point to add it to our list of "requests we've read but not yet
	 * replied to" -- although that *sounds* as if we should be doing it in
	 * kbus_read, we might never get round to reading the content of the
	 * message (we might call NEXTMSG again, or DISCARD), and also
	 * kbus_read can get called multiple times for a single message body.
	 * If we do our remembering here, then we guarantee to get one memory
	 * for each request, as it leaves the message queue and is (in whatever
	 * way) dealt with.
	 */
	if (msg->flags & KBUS_BIT_WANT_YOU_TO_REPLY) {
		retval = kbus_reply_needed(priv, msg);
		/* If it couldn't malloc, there's not much we can do,
		 * it's fairly fatal */
		if (retval)
			return retval;
	}

	/*
	 * If we (maybe) have any unread Replier Unbind Event messages,
	 * we now have room to copy one across to the message list
	 */
	kbus_maybe_dbg(priv->dev,
		       "  ++ maybe_got_unsent_unbind_msgs %d\n",
		       priv->maybe_got_unsent_unbind_msgs);

	if (priv->maybe_got_unsent_unbind_msgs) {
		retval = kbus_maybe_move_unsent_unbind_msg(priv);
		/* If this fails, we're probably stumped */
		if (retval)
			return retval;
	}

	retval = __put_user(KBUS_ENTIRE_MSG_LEN(msg->name_len, msg->data_len),
			    (u32 __user *) arg);
	if (retval)
		return retval;
	return 1;	/* We had a message */
}

/* How much of the current message is left to read? */
static u32 kbus_lenleft(struct kbus_private_data *priv)
{
	struct kbus_read_msg *this = &(priv->read);
	if (this->msg) {
		int ii, jj;
		u32 sofar = 0;
		u32 total = KBUS_ENTIRE_MSG_LEN(this->msg->name_len,
						     this->msg->data_len);
		/* Add up the items we're read all of, so far */
		for (ii = 0; ii < this->which; ii++) {
			if (this->which == KBUS_PART_DATA &&
			    this->msg->data_len > 0) {
				struct kbus_data_ptr *dp = this->msg->data_ref;
				for (jj = 0; jj < this->ref_data_index; jj++)
					sofar += dp->lengths[jj];
				if (this->ref_data_index < dp->num_parts)
					sofar += this->pos;
			} else {
				sofar += this->lengths[ii];
			}
		}
		/* Plus what we're read of the last one */
		if (this->which < KBUS_NUM_PARTS) {
			if (this->which == KBUS_PART_DATA &&
			    this->msg->data_len > 0) {
				struct kbus_data_ptr *dp = this->msg->data_ref;
				for (jj = 0; jj < this->ref_data_index; jj++)
					sofar += dp->lengths[jj];
				if (this->ref_data_index < dp->num_parts)
					sofar += this->pos;
			} else {
				sofar += this->pos;
			}
		}
		return total - sofar;
	}
	return 0; /* no message => nothing to read */
}

/*
 * Allocate the data arrays we need to hold reference-counted data, possibly
 * spread over multiple pages. 'data_len' is from the message header.
 *
 * Note that the 'lengths[n]' field to each page 'n' will be set to zero.
 */
static int kbus_alloc_ref_data(struct kbus_private_data *priv __maybe_unused,
			       u32 data_len,
			       struct kbus_data_ptr **ret_ref_data)
{
	int num_parts = 0;
	unsigned long *parts = NULL;
	unsigned *lengths = NULL;
	unsigned last_page_len = 0;
	struct kbus_data_ptr *ref_data = NULL;
	int as_pages;
	int ii;

	*ret_ref_data = NULL;

	num_parts = (data_len + KBUS_PART_LEN - 1) / KBUS_PART_LEN;

	/*
	 * To save recalculating the length of the last page every time
	 * we're interested, get it right once and for all.
	 */
	last_page_len = data_len - (num_parts - 1) * KBUS_PART_LEN;

	kbus_maybe_dbg(priv->dev,
		       "%u/%u Allocate ref data: part=%lu, "
		       "threshold=%lu, data_len %u -> num_parts %d\n",
		       priv->dev->index, priv->id, KBUS_PART_LEN,
		       KBUS_PAGE_THRESHOLD, data_len, num_parts);

	parts = kmalloc(sizeof(*parts) * num_parts, GFP_KERNEL);
	if (!parts)
		return -ENOMEM;
	lengths = kmalloc(sizeof(*lengths) * num_parts, GFP_KERNEL);
	if (!lengths) {
		kfree(parts);
		return -ENOMEM;
	}

	if (num_parts == 1 && data_len < KBUS_PAGE_THRESHOLD) {
		/* A single part in "simple" memory */
		as_pages = false;
		parts[0] = (unsigned long)kmalloc(data_len, GFP_KERNEL);
		if (!parts[0]) {
			kfree(lengths);
			kfree(parts);
			return -ENOMEM;
		}
		lengths[0] = 0;
	} else {
		/*
		 * One or more pages
		 *
		 * For simplicity, we make all of our pages be full pages.
		 * In theory, we could use the same rules for the last page
		 * as we do if we only have a single page - but for the
		 * moment, we're not bothering.
		 *
		 * This means that the 'last_page_len' is strictly theoretical
		 * for the moment...
		 */
		as_pages = true;
		for (ii = 0; ii < num_parts; ii++) {
			parts[ii] = __get_free_page(GFP_KERNEL);
			if (!parts[ii]) {
				int jj;
				for (jj = 0; jj < ii; jj++)
					free_page(parts[jj]);
				kfree(lengths);
				kfree(parts);
				return -ENOMEM;
			}
			lengths[ii] = 0;
		}
	}
	ref_data = kbus_wrap_data_in_ref(as_pages, num_parts, parts, lengths,
					 last_page_len);
	if (!ref_data) {
		int jj;
		if (as_pages)
			for (jj = 0; jj < num_parts; jj++)
				free_page(parts[jj]);
		else
			kfree((void *)parts[0]);
		kfree(lengths);
		kfree(parts);
		return -ENOMEM;
	}
	*ret_ref_data = ref_data;
	return 0;
}

/*
 * Does what it says on the box - take the user data and promote it to kernel
 * space, as a reference counted quantity, possibly spread over multiple pages.
 */
static int kbus_wrap_user_data(struct kbus_private_data *priv,
			       u32 data_len,
			       void *user_data_ptr,
			       struct kbus_data_ptr **new_data)
{
	struct kbus_data_ptr *ref_data = NULL;
	int num_parts;
	unsigned long *parts;
	unsigned *lengths;
	int ii;
	uint8_t __user *data_ptr;

	int retval = kbus_alloc_ref_data(priv, data_len, &ref_data);
	if (retval)
		return retval;

	num_parts = ref_data->num_parts;
	lengths = ref_data->lengths;
	parts = ref_data->parts;

	kbus_maybe_dbg(priv->dev, "  @@ copying %s\n",
		       ref_data->as_pages ? "as pages" : "as kmalloc'ed data");

	/* Given all of the *space* for our data, populate it */
	data_ptr = (void __user *) user_data_ptr;
	for (ii = 0; ii < num_parts; ii++) {
		unsigned len;
		if (ii == num_parts - 1)
			len = ref_data->last_page_len;
		else
			len = KBUS_PART_LEN;

		kbus_maybe_dbg(priv->dev,
			       "  @@ %d: copy %d bytes "
			       "from user address %lu\n",
			       ii, len, parts[ii]);

		if (copy_from_user((void *)parts[ii], data_ptr, len)) {
			kbus_lower_data_ref(ref_data);
			return -EFAULT;
		}
		lengths[ii] = len;
		data_ptr += len;
	}
	*new_data = ref_data;
	return 0;
}

/*
 * Given a "pointy" message header, copy the message name and data from
 * user space into kernel space.
 *
 * The message name is copied as a reference-counted string.
 *
 * The message data (if any) is copied as reference-counted data.
 *
 * Also checks the legality of the message name, since we need the name in
 * kernel space to do that, but prefer to do the check before copying any
 * data (which can be expensive).
 */
static int kbus_copy_pointy_parts(struct kbus_private_data *priv,
				  struct kbus_write_msg *this)
{
	struct kbus_msg *msg = this->msg;
	char *new_name = NULL;
	struct kbus_name_ptr *name_ref;
	struct kbus_data_ptr *new_data = NULL;

	/* First, let's deal with the name */
	new_name = kmalloc(msg->name_len + 1, GFP_KERNEL);
	if (!new_name)
		return -ENOMEM;
	if (copy_from_user
	    (new_name, (void __user *)this->user_name_ptr, msg->name_len + 1)) {
		kfree(new_name);
		return -EFAULT;
	}

	/*
	 * We can check the name now it is in kernel space - we want
	 * to do this before we sort out the data, since that can involve
	 * a *lot* of copying...
	 */
	if (kbus_invalid_message_name(priv->dev, new_name, msg->name_len)) {
		kfree(new_name);
		return -EBADMSG;
	}
	name_ref = kbus_wrap_name_in_ref(new_name);
	if (!name_ref) {
		kfree(new_name);
		return -ENOMEM;
	}

	/* Now for the data. */
	if (msg->data_len) {
		int retval = kbus_wrap_user_data(priv, msg->data_len,
						 this->user_data_ptr,
						 &new_data);
		if (retval) {
			kbus_lower_name_ref(name_ref);
			return retval;
		}
	}

	kbus_maybe_dbg(priv->dev, "  'pointy' message normalised\n");

	msg->name_ref = name_ref;
	msg->data_ref = new_data;

	this->user_name_ptr = NULL;
	this->user_data_ptr = NULL;
	this->pointers_are_local = true;

	return 0;
}

static void kbus_discard(struct kbus_private_data *priv)
{
	kbus_empty_write_msg(priv);
	priv->sending = false;
}

/*
 * Returns 0 for success, and a negative value if there's an error.
 */
static int kbus_send(struct kbus_private_data *priv,
		     struct kbus_dev *dev, unsigned long arg)
{
	ssize_t retval = 0;
	struct kbus_msg *msg = priv->write.msg;

	kbus_maybe_dbg(priv->dev, "%u/%u SEND\n", priv->dev->index,
		       priv->id);

	if (priv->write.msg == NULL)
		return -ENOMSG;

	if (!priv->write.is_finished) {
		dev_err(priv->dev->dev, "pid %u [%s]"
		       " message not finished (in part %d of message)\n",
		       current->pid, current->comm, priv->write.which);
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
	if (KBUS_BIT_SYNTHETIC & msg->flags)
		msg->flags &= ~KBUS_BIT_SYNTHETIC;

	/*
	 * The "extra" field is reserved for future expansion, so for the
	 * moment we always zero it (this stops anyone from trying to take
	 * advantage of it, and getting caught out when we decide WE want it)
	 */
	msg->extra = 0;

	/*
	 * The message header is already in kernel space (thanks to kbus_write),
	 * but if it's a "pointy" message, the name and data are not. So let's
	 * fix that.
	 *
	 * Note that we *always* end up with a message header containing
	 * pointers to (copies of) the name and (if given) data, and the
	 * data reference counted, and maybe split over multiple pages.
	 *
	 *     Note that if this is a message we already tried to send
	 *     earlier, any "pointy" parts would have been copied earlier,
	 *     hence the check we actually make.
	 */
	if (!priv->write.pointers_are_local) {
		retval = kbus_copy_pointy_parts(priv, &priv->write);
		if (retval)
			goto done;
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
	 *    *this Ksock* that needs to do some reading to clear the relevant
	 *    queue, and it can't do that if it's blocking. So we'd either
	 *    need to handle that (somehow), or just do the check here.
	 *
	 * Similarly, we don't finalise the message (put in its "from" and "id"
	 * fields) until we pass this test.
	 */
	if ((msg->flags & KBUS_BIT_WANT_A_REPLY) &&
	    kbus_queue_is_full(priv, "sender", false)) {
		dev_err(priv->dev->dev, "%u/%u Unable to send Request becausei"
			" no room for a Reply in sender's message queue\n",
			priv->dev->index, priv->id);
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
		if (msg->id.network_id == 0)
			msg->id.serial_num = kbus_next_serial_num(dev);
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
	if (retval == -EAGAIN)
		/* Remember we're still trying to send this message */
		priv->sending = true;
	else
		/* We've now finished with our copy of the message header */
		kbus_discard(priv);

	if (retval == 0 || retval == -EAGAIN)
		if (copy_to_user((void __user *)arg, &priv->last_msg_id_sent,
				 sizeof(priv->last_msg_id_sent)))
			retval = -EFAULT;
	return retval;
}

static int kbus_maxmsgs(struct kbus_private_data *priv,
			unsigned long arg)
{
	int retval = 0;
	u32 requested_max;

	retval = __get_user(requested_max, (u32 __user *) arg);
	if (retval)
		return retval;

	kbus_maybe_dbg(priv->dev, "%u/%u MAXMSGS requests %u (was %u)\n",
		       priv->dev->index, priv->id,
		       requested_max, priv->max_messages);

	/* A value of 0 is just a query for what the current length is */
	if (requested_max > 0)
		priv->max_messages = requested_max;

	return __put_user(priv->max_messages, (u32 __user *) arg);
}

static int kbus_nummsgs(struct kbus_private_data *priv,
			struct kbus_dev *dev __maybe_unused, unsigned long arg)
{
	u32 count = priv->message_count;

	if (priv->maybe_got_unsent_unbind_msgs) {
		kbus_maybe_dbg(dev, "%u/%u NUMMSGS 'main' count %u\n",
			       dev->index, priv->id, count);
		count += kbus_count_unsent_unbind_msgs(priv);
	}

	kbus_maybe_dbg(dev, "%u/%u NUMMSGS %u\n",
		       dev->index, priv->id, count);

	return __put_user(count, (u32 __user *) arg);
}

static int kbus_onlyonce(struct kbus_private_data *priv,
			 unsigned long arg)
{
	int retval = 0;
	u32 only_once;
	int old_value = priv->messages_only_once;

	retval = __get_user(only_once, (u32 __user *) arg);
	if (retval)
		return retval;

	kbus_maybe_dbg(priv->dev, "%u/%u ONLYONCE requests %u (was %d)\n",
		       priv->dev->index, priv->id, only_once, old_value);

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

	return __put_user(old_value, (u32 __user *) arg);
}

static int kbus_set_verbosity(struct kbus_private_data *priv,
			      unsigned long arg)
{
	int retval = 0;
	u32 verbose;
	int old_value = priv->dev->verbose;

	retval = __get_user(verbose, (u32 __user *) arg);
	if (retval)
		return retval;

	/*
	 * If we're *leaving* verbose mode, we should say so.
	 * However, we also want to  say if we're *entering* verbose
	 * mode, and that means we can't use kbus_maybe_dbg (since
	 * we're not yet in verbose mode)
	 */
#ifdef DEBUG
	dev_dbg(priv->dev->dev,
		"%u/%u VERBOSE requests %u (was %d)\n",
		priv->dev->index, priv->id, verbose, old_value);
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

	return __put_user(old_value, (u32 __user *) arg);
}

/* Report all existing replier bindings to the requester */
static int kbus_report_existing_binds(struct kbus_private_data *priv,
				      struct kbus_dev *dev)
{
	struct kbus_message_binding *ptr;
	struct kbus_message_binding *next;

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		struct kbus_msg *new_msg;
		int retval;

		kbus_maybe_dbg(priv->dev, "  %u/%u Report %c '%.*s'\n",
		       dev->index, priv->id, (ptr->is_replier ? 'R' : 'L'),
		       ptr->name_len, ptr->name);

		if (!ptr->is_replier)
			continue;

		new_msg = kbus_new_synthetic_bind_message(ptr->bound_to, true,
						  ptr->name_len, ptr->name);
		if (new_msg == NULL)
			return -ENOMEM;

		/*
		 * It is perhaps a bit inefficient to check this per binding,
		 * but it saves us doing two passes through the list.
		 */
		if (kbus_queue_is_full(priv, "limpet", false)) {
			/* Giving up is probably the best we can do */
			kbus_free_message(new_msg);
			return -EBUSY;
		}

		retval = kbus_push_message(priv, new_msg, NULL, false);

		kbus_free_message(new_msg);
		if (retval)
			return retval;
	}
	return 0;
}

static int kbus_set_report_binds(struct kbus_private_data *priv,
				 struct kbus_dev *dev, unsigned long arg)
{
	int retval = 0;
	u32 report_replier_binds;
	int old_value = priv->dev->report_replier_binds;

	retval = __get_user(report_replier_binds, (u32 __user *) arg);
	if (retval)
		return retval;

	kbus_maybe_dbg(priv->dev,
		       "%u/%u REPORTREPLIERBINDS requests %u (was %d)\n",
		       priv->dev->index, priv->id, report_replier_binds,
		       old_value);

	switch (report_replier_binds) {
	case 0:
		priv->dev->report_replier_binds = false;
		break;
	case 1:
		priv->dev->report_replier_binds = true;
		/* And report the current state of bindings... */
		retval = kbus_report_existing_binds(priv, dev);
		if (retval)
			return retval;
		break;
	case 0xFFFFFFFF:
		break;
	default:
		return -EINVAL;
	}

	return __put_user(old_value, (u32 __user *) arg);
}

static long kbus_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	int retval = 0;
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;
	u32 id = priv->id;

	if (_IOC_TYPE(cmd) != KBUS_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > KBUS_IOC_MAXNR)
		return -ENOTTY;
	/*
	 * Check our arguments at least vaguely match. Note that VERIFY_WRITE
	 * allows R/W transfers. Remember that 'type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and "write"
	 * is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg,
				_IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg,
				_IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	if (mutex_lock_interruptible(&dev->mux))
		return -ERESTARTSYS;

	switch (cmd) {

	case KBUS_IOC_RESET:
		/* This is currently a no-op, but may be useful later */
		kbus_maybe_dbg(priv->dev, "%u/%u RESET\n", dev->index,
			       id);
		break;

	case KBUS_IOC_BIND:
		/*
		 * BIND: indicate that a file wants to receive messages of a
		 * given name
		 */
		retval = kbus_bind(priv, dev, arg);
		break;

	case KBUS_IOC_UNBIND:
		/*
		 * UNBIND: indicate that a file no longer wants to receive
		 * messages of a given name
		 */
		retval = kbus_unbind(priv, dev, arg);
		break;

	case KBUS_IOC_KSOCKID:
		/*
		 * What is the "Ksock id" for this file descriptor
		 */
		kbus_maybe_dbg(priv->dev, "%u/%u KSOCKID %u\n",
			       dev->index, id, id);
		retval = __put_user(id, (u32 __user *) arg);
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
		retval = kbus_nextmsg(priv, arg);
		break;

	case KBUS_IOC_LENLEFT:
		/* How many bytes are left to read in the current message? */
		{
			u32 left = kbus_lenleft(priv);
			kbus_maybe_dbg(priv->dev, "%u/%u LENLEFT %u\n",
				       dev->index, id, left);
			retval = __put_user(left, (u32 __user *) arg);
		}
		break;

	case KBUS_IOC_SEND:
		/*
		 * Send the curent message, we've finished writing it.
		 *
		 * arg in: <ignored>
		 * arg out: the message id of said message
		 * retval: negative for bad message, etc., 0 otherwise
		 */
		retval = kbus_send(priv, dev, arg);
		break;

	case KBUS_IOC_DISCARD:
		/* Throw away the message we're currently writing. */
		kbus_maybe_dbg(priv->dev, "%u/%u DISCARD\n", dev->index,
			       id);
		kbus_discard(priv);
		break;

	case KBUS_IOC_LASTSENT:
		/*
		 * What was the message id of the last message written to this
		 * file descriptor? Before any messages have been written to
		 * this file descriptor, this ioctl will return {0,0).
		 */
		kbus_maybe_dbg(priv->dev, "%u/%u LASTSENT %u:%u\n",
			       dev->index, id,
			       priv->last_msg_id_sent.network_id,
			       priv->last_msg_id_sent.serial_num);
		if (copy_to_user((void __user *)arg, &priv->last_msg_id_sent,
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
		retval = kbus_maxmsgs(priv, arg);
		break;

	case KBUS_IOC_NUMMSGS:
		/* How many messages are in our queue?
		 *
		 * arg out: maximum number allowed
		 * return: 0 means OK, otherwise not OK
		 */
		retval = kbus_nummsgs(priv, dev, arg);
		break;

	case KBUS_IOC_UNREPLIEDTO:
		/* How many Requests (to us) do we still owe Replies to? */
		kbus_maybe_dbg(priv->dev, "%u/%u UNREPLIEDTO %d\n",
			       dev->index, id, priv->num_replies_unsent);
		retval = __put_user(priv->num_replies_unsent,
				(u32 __user *) arg);
		break;

	case KBUS_IOC_MSGONLYONCE:
		/*
		 * Should we receive a given message only once?
		 *
		 * arg in: 0 (for no), 1 (for yes), 0xFFFFFFFF (for query)
		 * arg out: the previous value, before we were called
		 * return: 0 means OK, otherwise not OK
		 */
		retval = kbus_onlyonce(priv, arg);
		break;

	case KBUS_IOC_VERBOSE:
		/*
		 * Should we output verbose/debug messages?
		 *
		 * arg in: 0 (for no), 1 (for yes), 0xFFFFFFFF (for query)
		 * arg out: the previous value, before we were called
		 * return: 0 means OK, otherwise not OK
		 */
		retval = kbus_set_verbosity(priv, arg);
		break;

	case KBUS_IOC_NEWDEVICE:
		/*
		 * Request a new device
		 *
		 * arg out: the new device number
		 * return: 0 means OK, otherwise not OK.
		 */
		kbus_maybe_dbg(priv->dev, "%u/%u NEWDEVICE %d\n",
			       dev->index, id, kbus_num_devices);
		retval = kbus_setup_new_device(kbus_num_devices);
		if (retval > 0) {
			kbus_num_devices++;
			retval = __put_user(kbus_num_devices - 1,
				       (u32 __user *) arg);
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

	default:
		/* *Should* be redundant, if we got our range checks right */
		retval = -ENOTTY;
		break;
	}

	mutex_unlock(&dev->mux);
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
static int kbus_poll_try_send_again(struct kbus_private_data *priv,
				    struct kbus_dev *dev)
{
	int retval;
	struct kbus_msg *msg = priv->write.msg;

	retval = kbus_write_to_recipients(priv, dev, msg);

	switch (-retval) {
	case 0:		/* All is well, nothing to do */
		break;
	case EAGAIN:		/* Still blocked by *someone* - nowt to do */
		break;
	case EADDRNOTAVAIL:
		/*
		 * It's a Request and there's no Replier (presumably there was
		 * when the initial SEND was done, but now they've gone away).
		 * A Request *needs* a Reply...
		 */
		kbus_push_synthetic_message(dev, 0, msg->from, msg->id,
					    KBUS_MSG_NAME_REPLIER_DISAPPEARED);
		retval = 0;
		break;
	default:
		/*
		 * Send *failed* - what can we do?
		 * Not much, perhaps, but we must ensure that a Request gets
		 * (some sort of) reply
		 */
		if (msg->flags & KBUS_BIT_WANT_A_REPLY)
			kbus_push_synthetic_message(dev, 0, msg->from, msg->id,
					    KBUS_MSG_NAME_ERROR_SENDING);
		retval = 0;
		break;
	}

	if (retval == 0) {
		kbus_discard(priv);
		return true;
	}
	return false;
}

static unsigned int kbus_poll(struct file *filp, poll_table * wait)
{
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;
	unsigned mask = 0;

	mutex_lock(&dev->mux);

	kbus_maybe_dbg(priv->dev, "%u/%u POLL\n", dev->index, priv->id);

	/*
	 * Did I wake up because there's a message available to be read?
	 */
	if (priv->message_count != 0)
		mask |= POLLIN | POLLRDNORM;	/* readable */

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
		int writable = true;
		if (priv->sending)
			writable = kbus_poll_try_send_again(priv, dev);
		if (writable)
			mask |= POLLOUT | POLLWRNORM;
	}

	/* Wait until someone has a message waiting to be read */
	poll_wait(filp, &priv->read_wait, wait);

	/* Wait until someone has a space into which a message can be pushed */
	if (priv->sending)
		poll_wait(filp, &dev->write_wait, wait);

	mutex_unlock(&dev->mux);
	return mask;
}

/* File operations for /dev/kbus<n> */
static const struct file_operations kbus_fops = {
	.owner = THIS_MODULE,
	.read = kbus_read,
	.write = kbus_write,
	.unlocked_ioctl = kbus_ioctl,
	.poll = kbus_poll,
	.open = kbus_open,
	.release = kbus_release,
};

static void kbus_setup_cdev(struct kbus_dev *dev, int devno)
{
	int err;

	/*
	 * Remember to initialise the mutex *before* making the device
	 * available!
	 */
	mutex_init(&dev->mux);

	/*
	 * This seems like a sensible place to setup other device specific
	 * stuff, too.
	 */
	INIT_LIST_HEAD(&dev->bound_message_list);
	INIT_LIST_HEAD(&dev->open_ksock_list);
	INIT_LIST_HEAD(&dev->unsent_unbind_msg_list);

	init_waitqueue_head(&dev->write_wait);

	dev->next_ksock_id = 0;
	dev->next_msg_serial_num = 0;

	cdev_init(&dev->cdev, &kbus_fops);
	dev->cdev.owner = THIS_MODULE;

	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		pr_err("Error %d adding kbus0 as a character device\n",
		       err);
}

static void kbus_teardown_cdev(struct kbus_dev *dev)
{
	cdev_del(&dev->cdev);

	kbus_forget_all_bindings(dev);
	kbus_forget_all_open_ksocks(dev);
	kbus_forget_unsent_unbind_msgs(dev);
}

/* ========================================================================= */
/* PROC */

/*
 * Report on the current bindings, via /proc/kbus/bindings
 */

static int kbus_binding_seq_show(struct seq_file *s, void *v __always_unused)
{
	int ii;

	/* We report on all of the KBUS devices */
	for (ii = 0; ii < kbus_num_devices; ii++) {
		struct kbus_dev *dev = kbus_devices[ii];

		struct kbus_message_binding *ptr;
		struct kbus_message_binding *next;

		if (mutex_lock_interruptible(&dev->mux))
			return -ERESTARTSYS;

		seq_printf(s,
			   "# <device> is bound to <Ksock-ID> in <process-PID>"
			   " as <Replier|Listener> for <message-name>\n");

		list_for_each_entry_safe(ptr, next, &dev->bound_message_list,
					 list) {
			seq_printf(s, "%3u: %8u %8lu  %c  %.*s\n", dev->index,
				   ptr->bound_to_id,
				   (long unsigned)ptr->bound_to->pid,
				   (ptr->is_replier ? 'R' : 'L'),
				   ptr->name_len, ptr->name);
		}

		mutex_unlock(&dev->mux);
	}
	return 0;
}

static int kbus_proc_bindings_open(struct inode *inode __always_unused,
				   struct file *file)
{
	return single_open(file, kbus_binding_seq_show, NULL);
}

static const struct file_operations kbus_proc_binding_file_ops = {
	.owner = THIS_MODULE,
	.open = kbus_proc_bindings_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

static struct proc_dir_entry
*kbus_create_proc_binding_file(struct proc_dir_entry *directory)
{
	struct proc_dir_entry *entry =
	    create_proc_entry("bindings", 0, directory);
	if (entry)
		entry->proc_fops = &kbus_proc_binding_file_ops;
	return entry;
}

/*
 * Report on whatever statistics seem like they might be useful,
 * via /proc/kbus/stats
 */

static int kbus_stats_seq_show(struct seq_file *s, void *v __always_unused)
{
	int ii;

	/* We report on all of the KBUS devices */
	for (ii = 0; ii < kbus_num_devices; ii++) {
		struct kbus_dev *dev = kbus_devices[ii];

		struct kbus_private_data *ptr;
		struct kbus_private_data *next;

		if (mutex_lock_interruptible(&dev->mux))
			return -ERESTARTSYS;

		seq_printf(s,
			 "dev %2u: next ksock %u next msg %u "
			 "unsent unbindings %u%s\n",
			 dev->index, dev->next_ksock_id,
			 dev->next_msg_serial_num,
			 dev->unsent_unbind_msg_count,
			 (dev->unsent_unbind_is_tragic ? "(gone tragic)" : ""));

		list_for_each_entry_safe(ptr, next,
					 &dev->open_ksock_list, list) {

			u32 left = kbus_lenleft(ptr);
			u32 total;
			if (ptr->read.msg)
				total =
				    KBUS_ENTIRE_MSG_LEN(ptr->read.msg->name_len,
						ptr->read.msg->data_len);
			else
				total = 0;

			seq_printf(s, "    ksock %u last msg %u:%u "
					"queue %u of %u\n",
				   ptr->id, ptr->last_msg_id_sent.network_id,
				   ptr->last_msg_id_sent.serial_num,
				   ptr->message_count, ptr->max_messages);

			seq_printf(s, "      read byte %u of %u, "
				"wrote byte %u of %s (%sfinished), "
				"%ssending\n",
				   (total - left), total, ptr->write.pos,
				   kbus_msg_part_name(ptr->write.which),
				   ptr->write.is_finished ? "" : "not ",
				   ptr->sending ? "" : "not ");

			seq_printf(s, "      outstanding requests %u "
					"(size %u, max %u), "
					"unsent replies %u (max %u)\n",
					ptr->outstanding_requests.count,
					ptr->outstanding_requests.size,
					ptr->outstanding_requests.max_count,
					ptr->num_replies_unsent,
					ptr->max_replies_unsent);
		}
		mutex_unlock(&dev->mux);
	}

	return 0;
}

static int kbus_proc_stats_open(struct inode *inode __always_unused,
				struct file *file)
{
	return single_open(file, kbus_stats_seq_show, NULL);
}

static const struct file_operations kbus_proc_stats_file_ops = {
	.owner = THIS_MODULE,
	.open = kbus_proc_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

static struct proc_dir_entry
*kbus_create_proc_stats_file(struct proc_dir_entry *directory)
{
	struct proc_dir_entry *entry = create_proc_entry("stats", 0, directory);
	if (entry)
		entry->proc_fops = &kbus_proc_stats_file_ops;
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
	struct kbus_dev *new = NULL;
	dev_t this_devno;

	if (which < 0 || which > (KBUS_MAX_NUM_DEVICES - 1)) {
		pr_err("kbus: next device index %d not %d..%d\n",
		       which, KBUS_MIN_NUM_DEVICES, KBUS_MAX_NUM_DEVICES);
		return -EINVAL;
	}

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	memset(new, 0, sizeof(*new));

	/* Connect the device up with its operations */
	this_devno = MKDEV(kbus_major, kbus_minor + which);
	kbus_setup_cdev(new, this_devno);
	new->index = which;

	new->verbose = KBUS_DEFAULT_VERBOSE_SETTING;

	new->dev = device_create(kbus_class_p, NULL,
				 this_devno, NULL, "kbus%d", which);

	kbus_devices[which] = new;
	return which;
}

static int __init kbus_init(void)
{
	int result;
	int ii;
	dev_t devno = 0;

	pr_notice("Initialising KBUS module (%d device%s)\n",
		  kbus_num_devices, kbus_num_devices == 1 ? "" : "s");
	/* This allows hackers to see rmmod/insmod transitions.
	 * Not to be enabled by default! */

	if (kbus_num_devices < KBUS_MIN_NUM_DEVICES ||
	    kbus_num_devices > KBUS_MAX_NUM_DEVICES) {
		pr_err("kbus: requested number of devices %d not %d..%d\n",
		       kbus_num_devices,
		       KBUS_MIN_NUM_DEVICES, KBUS_MAX_NUM_DEVICES);
		return -EINVAL;
	}

	/* ================================================================= */
	/*
	 * Our main purpose is to provide /dev/kbus
	 * We wish to start our device numbering with device 0, and device 0
	 * should always be present,
	 */
	result = alloc_chrdev_region(&devno, kbus_minor, KBUS_MAX_NUM_DEVICES,
				     "kbus");
	/* We're quite happy with dynamic allocation of our major number */
	kbus_major = MAJOR(devno);
	if (result < 0) {
		pr_warn("kbus: Cannot allocate character device region "
		       "(error %d)\n", -result);
		return result;
	}

	kbus_devices = kmalloc(KBUS_MAX_NUM_DEVICES * sizeof(struct kbus_dev *),
			       GFP_KERNEL);
	if (!kbus_devices) {
		pr_warn("kbus: Cannot allocate devices\n");
		unregister_chrdev_region(devno, kbus_num_devices);
		return -ENOMEM;
	}
	memset(kbus_devices, 0, kbus_num_devices * sizeof(struct kbus_dev *));

	/*
	 * To make the user's life as simple as possible, let's make our device
	 * hot pluggable -- this means that on a modern system it *should* just
	 * appear, as if by magic (and go away again when the module is
	 * removed).
	 */
	kbus_class_p = class_create(THIS_MODULE, "kbus");
	if (IS_ERR(kbus_class_p)) {
		long err = PTR_ERR(kbus_class_p);
		if (err == -EEXIST) {
			pr_warn("kbus: Cannot create kbus class, "
			       "it already exists\n");
		} else {
			pr_err("kbus: Error creating kbus class\n");
			unregister_chrdev_region(devno, kbus_num_devices);
			return err;
		}
	}

	/* And connect up the number of devices we've been asked for */
	for (ii = 0; ii < kbus_num_devices; ii++) {
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

	int ii;
	dev_t devno = MKDEV(kbus_major, kbus_minor);

	pr_notice("Standing down kbus module\n");

	/*
	 * If I'm destroying the class, do I actually need to destroy the
	 * individual device therein first? Best safe...
	 */
	for (ii = 0; ii < kbus_num_devices; ii++) {
		dev_t this_devno = MKDEV(kbus_major, kbus_minor + ii);
		device_destroy(kbus_class_p, this_devno);
	}
	class_destroy(kbus_class_p);

	for (ii = 0; ii < kbus_num_devices; ii++) {
		kbus_teardown_cdev(kbus_devices[ii]);
		kfree(kbus_devices[ii]);
	}
	unregister_chrdev_region(devno, kbus_num_devices);

	if (kbus_proc_dir) {
		if (kbus_proc_file_bindings)
			remove_proc_entry("bindings", kbus_proc_dir);
		if (kbus_proc_file_stats)
			remove_proc_entry("stats", kbus_proc_dir);
		remove_proc_entry("kbus", NULL);
	}
}

module_param(kbus_num_devices, int, S_IRUGO);
MODULE_PARM_DESC(kbus_num_devices,
		"Number of KBUS device nodes to initially create");
module_init(kbus_init);
module_exit(kbus_exit);

MODULE_DESCRIPTION("KBUS lightweight messaging system");
MODULE_AUTHOR("tibs@tonyibbs.co.uk, tony.ibbs@gmail.com");
/*
 * All well-behaved Linux kernel modules should be licensed under GPL v2.
 * So shall it be.
 *
 * (According to the comments in <linux/module.h>, the "v2" is implicit here)
 *
 * We also license under the MPL, to allow free use outwith Linux if anyone
 * wishes.
 */
MODULE_LICENSE("Dual MPL/GPL");
