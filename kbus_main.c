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

#include <linux/kbus_defns.h>
#include "kbus_internal.h"

static int kbus_num_devices = CONFIG_KBUS_DEF_NUM_DEVICES;

/* Who we are -- devices */
static int kbus_major;	/* 0 => We'll go for dynamic allocation */
static int kbus_minor;	/* 0 => We're happy to start with device 0 */

/* Our actual devices, 0 through kbus_num_devices-1 */
static struct kbus_dev **kbus_devices;

static struct class *kbus_class_p;

/* ========================================================================= */

/* I really want this function where it is in the code, so need to foreshadow */
static int kbus_setup_new_device(int which);

/* ========================================================================= */

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

		kbus_maybe_dbg(dev, "  '%.*s' has replier %u\n",
			       ptr->name_len, ptr->name, ptr->bound_to_id);
		*bound_to = ptr->bound_to;
		return 1;
	}
	return 0;
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
				       "%u CANNOT BIND '%.*s' as "
				       "replier, already bound\n",
				       priv->id, name_len, name);
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

		kbus_maybe_dbg(priv->dev, "  %u Found %c '%.*s'\n",
			       priv->id, (ptr->is_replier ? 'R' : 'L'),
			       ptr->name_len, ptr->name);
		return ptr;
	}
	return NULL;
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
			       "  %u Could not find/unbind "
			       "%u %c '%.*s'\n",
			       priv->id, priv->id,
			       (replier ? 'R' : 'L'), name_len, name);
		return -EINVAL;
	}

	kbus_maybe_dbg(priv->dev, "  %u Unbound %u %c '%.*s'\n",
		       priv->id, binding->bound_to_id,
		       (binding->is_replier ? 'R' : 'L'),
		       binding->name_len, binding->name);

	/*
	 * If we supported sending messages (yet), we'd need to forget
	 * any messages in our queue that match this binding.
	 */

	/* And remove the binding once that has been done. */
	list_del(&binding->list);
	kfree(binding->name);
	kfree(binding);
	return 0;
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

	kbus_maybe_dbg(dev, "%u Forgetting my bindings\n", priv->id);

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {
		if (bound_to_id != ptr->bound_to_id)
			continue;

		kbus_maybe_dbg(dev, "  Unbound %u %c '%.*s'\n",
			       ptr->bound_to_id, (ptr->is_replier ? 'R' : 'L'),
			       ptr->name_len, ptr->name);

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

	kbus_maybe_dbg(dev, "Forgetting bindings\n");

	list_for_each_entry_safe(ptr, next, &dev->bound_message_list, list) {

		kbus_maybe_dbg(dev, "  Unbinding %u %c '%.*s'\n",
			       ptr->bound_to_id,
			       (ptr->is_replier ? 'R' : 'L'),
			       ptr->name_len, ptr->name);

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
 * Remove an open file remembrance.
 *
 * Returns 0 if all went well, -EINVAL if we couldn't find the open Ksock
 */
static int kbus_forget_open_ksock(struct kbus_dev *dev, u32 id)
{
	struct kbus_private_data *ptr;
	struct kbus_private_data *next;

	list_for_each_entry_safe(ptr, next, &dev->open_ksock_list, list) {
		if (id != ptr->id)
			continue;

		kbus_maybe_dbg(dev, "  Forgetting open Ksock %u\n", id);

		/* So remove it from our list */
		list_del(&ptr->list);
		/* But *we* mustn't free the actual datastructure! */
		return 0;
	}
	kbus_maybe_dbg(dev, "  Could not forget open Ksock %u\n", id);

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

		kbus_maybe_dbg(dev, "  Forgetting open Ksock %u\n", ptr->id);

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

	INIT_LIST_HEAD(&priv->message_queue);
	INIT_LIST_HEAD(&priv->replies_unsent);

	(void)kbus_remember_open_ksock(dev, priv);

	filp->private_data = priv;

	mutex_unlock(&dev->mux);

	kbus_maybe_dbg(dev, "%u OPEN\n", priv->id);

	return 0;
}

static int kbus_release(struct inode *inode __always_unused, struct file *filp)
{
	int retval2 = 0;
	struct kbus_private_data *priv = filp->private_data;
	struct kbus_dev *dev = priv->dev;

	if (mutex_lock_interruptible(&dev->mux))
		return -ERESTARTSYS;

	kbus_maybe_dbg(dev, "%u RELEASE\n", priv->id);

	kbus_forget_my_bindings(priv);
	retval2 = kbus_forget_open_ksock(dev, priv->id);
	kfree(priv);

	mutex_unlock(&dev->mux);

	return retval2;
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

	kbus_maybe_dbg(priv->dev, "%u BIND %c '%.*s'\n", priv->id,
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

	kbus_maybe_dbg(priv->dev, "%u UNBIND %c '%.*s'\n", priv->id,
		       (bind->is_replier ? 'R' : 'L'), bind->name_len, name);

	retval = kbus_forget_binding(dev, priv,
				     bind->is_replier, bind->name_len, name);

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

	kbus_maybe_dbg(priv->dev, "%u REPLIER for '%.*s'\n",
		       priv->id, query->name_len, name);

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

/* How much of the current message is left to read? */
extern u32 kbus_lenleft(struct kbus_private_data *priv)
{
	return 0; /* no message => nothing to read */
}

static int kbus_maxmsgs(struct kbus_private_data *priv,
			unsigned long arg)
{
	int retval = 0;
	u32 requested_max;

	retval = __get_user(requested_max, (u32 __user *) arg);
	if (retval)
		return retval;

	kbus_maybe_dbg(priv->dev, "%u MAXMSGS requests %u (was %u)\n",
		       priv->id, requested_max, priv->max_messages);

	/* A value of 0 is just a query for what the current length is */
	if (requested_max > 0)
		priv->max_messages = requested_max;

	return __put_user(priv->max_messages, (u32 __user *) arg);
}

static int kbus_nummsgs(struct kbus_private_data *priv,
			struct kbus_dev *dev __maybe_unused, unsigned long arg)
{
	u32 count = priv->message_count;

	kbus_maybe_dbg(dev, "%u NUMMSGS %u\n", priv->id, count);

	return __put_user(count, (u32 __user *) arg);
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
		"%u VERBOSE requests %u (was %d)\n",
		priv->id, verbose, old_value);
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
		kbus_maybe_dbg(priv->dev, "%u RESET\n", id);
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
		kbus_maybe_dbg(priv->dev, "%u KSOCKID %u\n", id, id);
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

	default:
		/* *Should* be redundant, if we got our range checks right */
		retval = -ENOTTY;
		break;
	}

	mutex_unlock(&dev->mux);
	return retval;
}

/* File operations for /dev/kbus<n> */
static const struct file_operations kbus_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = kbus_ioctl,
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
	kbus_forget_all_bindings(dev);
	kbus_forget_all_open_ksocks(dev);

	cdev_del(&dev->cdev);
}


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

/* Allow the reporting infrastructure to "see" our internals */
extern void kbus_get_device_data(int *num_devices,
				 struct kbus_dev ***devices)
{
	*num_devices = kbus_num_devices;
	*devices = kbus_devices;
}

static int __init kbus_init(void)
{
	int result;
	int ii;
	dev_t devno = 0;

#ifdef DEBUG
	pr_notice("Initialising KBUS module (%d device%s)\n",
		  kbus_num_devices, kbus_num_devices == 1 ? "" : "s");
#endif

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

	/* Set up the files that allow users to see something of our state */
	kbus_setup_reporting();

	return 0;
}

static void __exit kbus_exit(void)
{
	/* No locking done, as we're standing down */

	int ii;
	dev_t devno = MKDEV(kbus_major, kbus_minor);

#ifdef DEBUG
	pr_notice("Standing down kbus module\n");
#endif

	for (ii = 0; ii < kbus_num_devices; ii++) {
		kbus_teardown_cdev(kbus_devices[ii]);
		kfree(kbus_devices[ii]);
	}
	unregister_chrdev_region(devno, kbus_num_devices);

	/*
	 * If I'm destroying the class, do I actually need to destroy the
	 * individual device therein? Best safe...
	 */
	for (ii = 0; ii < kbus_num_devices; ii++) {
		dev_t this_devno = MKDEV(kbus_major, kbus_minor + ii);
		device_destroy(kbus_class_p, this_devno);
	}
	class_destroy(kbus_class_p);

	kbus_remove_reporting();
}

module_param(kbus_num_devices, int, S_IRUGO);
MODULE_PARM_DESC(kbus_num_devices,
		"Number of KBUS device nodes to create initially");
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
