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

typedef int ksock;

/* KSock Funcitons */
ksock kbus_ksock_open           (const char *fname, int flags);
int   kbus_ksock_close          (ksock ks);
int   kbus_ksock_bind           (ksock ks, const char *name);
int   kbus_ksock_id             (ksock ks, uint32_t *ksock_id);
int   kbus_ksock_next_msg       (ksock ks, uint32_t *len);
int   kbus_ksock_read_next_msg  (ksock ks, struct kbus_message_header **kms);
int   kbus_ksock_read_msg       (ksock ks, struct kbus_message_header **kms, 
                                 size_t len);
int   kbus_ksock_write_msg      (ksock ks, 
				 const struct kbus_message_header *kms);
int   kbus_ksock_send_msg       (ksock ks, 
				 const struct kbus_message_header *kms, 
				 struct kbus_msg_id *msg_id);
int   kbus_ksock_send           (ksock ks, struct kbus_msg_id *msg_id);


/* Message Functions*/
int kbus_msg_create(struct kbus_message_header **kms, 
		    const char *name, uint32_t name_len, /* bytes */
		    const void *data, uint32_t data_len, /* bytes */
		    uint32_t flags);

void   kbus_msg_dump(const struct kbus_message_header *kms, int dump_data);


#ifdef __cplusplus
}
#endif 

#endif /* _LKBUS_H_INCLUDED_ */
