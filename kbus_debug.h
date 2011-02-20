/* KBUS kernel module - debug message handling
 *
 * This header sets up KBUS debug message handling. As such
 * it is only expected to be included by kbus.c itself.
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

#ifndef _kbus_debug
#define _kbus_debug

#ifndef CONFIG_KBUS
	/*
	 * We're not building in-tree, so none of our CONFIG_* are set.
	 * Default to allow debug
	 */
#define CONFIG_KBUS_DEBUG
#endif

#ifdef CONFIG_KBUS_DEBUG
#define DEBUG 1
#define kbus_maybe_dbg(kbus_dev, format, args...) do { \
	if ((kbus_dev)->verbose) \
		(void) dev_dbg((kbus_dev)->dev, format, ## args); \
} while (0)
#else
#define DEBUG 0
#define kbus_maybe_dbg(kbus_dev, format, args...) ((void)0)
#endif

/* Should we default to verbose? */
#ifdef CONFIG_KBUS_DEBUG_DEFAULT_VERBOSE
#define KBUS_DEBUG_DEFAULT_SETTING true
#else
#define KBUS_DEBUG_DEFAULT_SETTING false
#endif

#endif /* _kbus_debug */
