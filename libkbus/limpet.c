/*
 * Support for KBUS Limpets - proxying messages between KBUS devices, possibly
 * on different machines.
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
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Kynesim, Cambridge UK
 *   Tony Ibbs <tibs@tonyibbs.co.uk>
 *
 * ***** END LICENSE BLOCK *****
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>     // for sockaddr_un
#include <netinet/in.h> // for sockaddr_in
#include <netdb.h>
#include <poll.h>

#include "libkbus/kbus.h"
#include "limpet.h"

// Quick to code, slow to execute, should do for now
struct replier_for {
    char                *name;      // the message name
    uint32_t             binder;    // who is bound as a replier for it
    struct replier_for  *next;      // and another of us...
};
typedef struct replier_for replier_for_t;

// Ditto, more or less
struct request_from {
    kbus_msg_id_t        id;        // the Request message's id
    uint32_t             from;      // who it was from
    struct request_from *next;      // and another of us...
};
typedef struct request_from request_from_t;


struct kbus_limpet_context {
    int              socket;             // Our connection to the other limpet
    kbus_ksock_t     ksock;              // Our connection to KBUS
    uint32_t         ksock_id;           // Our own ksock id
    uint32_t         network_id;         // Our network id
    uint32_t         other_network_id;   // The other limpet's network id
    char            *message_name;       // The message name we're filtering on
    replier_for_t   *replier_for;        // Message repliers
    request_from_t  *request_from;       // Requests we're expecting replies for
    int              verbosity;          // 0=quiet, 1=normal, 2=lots
};

static void print_replier_for(kbus_limpet_context_t    *context)
{
    replier_for_t    *this = context->replier_for;
    while (this) {
        if (context->verbosity > 1)
            printf("%u .. %4u is replier for '%s'\n", context->network_id,
                   this->binder, this->name);
        this = this->next;
    }
}

// Return the binder id, or 0 if we can't find one.
static uint32_t find_replier_for(kbus_limpet_context_t *context,
                                 char                  *name)
{
    replier_for_t    *this = context->replier_for;
    while (this) {
        if (!strcmp(name, this->name)) {
            return this->binder;
        }
        this = this->next;
    }
    return 0;
}

static int forget_replier_for(kbus_limpet_context_t *context,
                              char                  *name)
{
    replier_for_t    *prev = NULL;
    replier_for_t    *this = context->replier_for;

    while (this) {
        if (!strcmp(name, this->name)) {

            if (prev == NULL)
                context->replier_for = this->next;
            else
                prev->next = this->next;

            free(this->name);
            free(this);
            return 0;
        }
        prev = this;
        this = this->next;
    }
    if (context->verbosity)
        printf("Limpet %u: Unable to find entry for replier binding '%s' to delete\n",
               context->network_id, name);
    return -1;
}

// If we succeed, then we "own" the name, and the caller should not free it
static int remember_replier_for(kbus_limpet_context_t    *context,
                                char                *name,
                                uint32_t             binder)
{
    replier_for_t    *new = NULL;

    if (find_replier_for(context, name)) {
        // We decide that it's an error to already have an entry
        if (context->verbosity)
            printf("Limpet %u: Attempt to remember another binder for '%s'\n",
                   context->network_id, name);
        return -1;
    }

    new = malloc(sizeof(*new));
    if (!new) {
        if (context->verbosity)
            printf("Limpet %u: Cannot allocate memory for remembering a binding\n",
                   context->network_id);
        return -ENOMEM;
    }

    new->name = name;
    new->binder = binder;
    new->next = context->replier_for;

    context->replier_for = new;

    return 0;
}

static void forget_all_replier_for(kbus_limpet_context_t *context)
{
    replier_for_t   *ptr = context->replier_for;
    while (ptr) {
        replier_for_t   *next = ptr->next;
        free(ptr->name);
        free(ptr);
        ptr = next;
    }
}

static void print_request_from(kbus_limpet_context_t    *context)
{
    request_from_t    *this = context->request_from;
    while (this) {
        if (context->verbosity > 1)
            printf("%u .. message [%u:%u] was from %u\n", context->network_id,
                   this->id.network_id, this->id.serial_num, this->from);
        this = this->next;
    }
}

// Return the 'from' id, or 0 if we can't find one.
static uint32_t find_request_from(kbus_limpet_context_t    *context,
                                  kbus_msg_id_t        id)
{
    request_from_t    *this = context->request_from;
    while (this) {
        if (!kbus_msg_compare_ids(&id, &this->id)) {
            return this->from;
        }
        this = this->next;
    }
    return 0;
}

static int forget_request_from(kbus_limpet_context_t  *context,
                               kbus_msg_id_t      id)
{
    request_from_t    *prev = NULL;
    request_from_t    *this = context->request_from;

    while (this) {
        if (!kbus_msg_compare_ids(&id, &this->id)) {  // Assume just one match

            if (prev == NULL)
                context->request_from = this->next;
            else
                prev->next = this->next;

            free(this);
            return 0;
        }
        prev = this;
        this = this->next;
    }
    if (context->verbosity)
        printf("Limpet %u: Unable to find entry for request from [%u:%u] to delete\n",
               context->network_id, id.network_id, id.serial_num);
    return -1;
}

static int remember_request_from(kbus_limpet_context_t    *context,
                                 kbus_msg_id_t        id,
                                 uint32_t             from)
{
    request_from_t    *new = NULL;

    if (find_request_from(context, id)) {
        // We decide that it's an error to already have an entry
        if (context->verbosity)
            printf("Limpet %u: Attempt to remember another request 'from' for [%u:%u]\n",
                   context->network_id, id.network_id, id.serial_num);
        return -1;
    }

    new = malloc(sizeof(*new));
    if (!new) {
        if (context->verbosity)
            printf("Limpet %u: Cannot allocate memory for remembering a binding\n",
                   context->network_id);
        return -ENOMEM;
    }

    new->id = id;
    new->from = from;
    new->next = context->request_from;

    context->request_from = new;

    return 0;
}

static void forget_all_request_from(kbus_limpet_context_t *context)
{
    request_from_t   *ptr = context->request_from;
    while (ptr) {
        request_from_t   *next = ptr->next;
        free(ptr);
        ptr = next;
    }
}

/*
 * Given a KBUS message, set the `result` array to its content, suitable for
 * sending across the network
 *
 * Ignores the message's name and data pointers.
 *
 * Thus we end up with::
 *
 *  result[0]  = msg->start_guard
 *  result[1]  = msg->id.network_id
 *  result[2]  = msg->id.serial_num
 *  result[3]  = msg->in_reply_to.network_id
 *  result[4]  = msg->in_reply_to.serial_num
 *  result[5]  = msg->to
 *  result[6]  = msg->from
 *  result[7]  = msg->orig_from.network_id
 *  result[8]  = msg->orig_from.local_id
 *  result[9]  = msg->final_to.network_id
 *  result[10] = msg->final_to.local_id
 *  result[11] = msg->extra
 *  result[12] = msg->flags
 *  result[13] = msg->name_len
 *  result[14] = msg->data_len
 *  result[15] = msg->end_guard
 */
extern void kbus_serialise_message_header(kbus_message_t *msg,
                                          uint32_t        result[KBUS_SERIALISED_HDR_LEN])
{
    int ii;

    result[0]  = msg->start_guard;
    result[1]  = msg->id.network_id;
    result[2]  = msg->id.serial_num;
    result[3]  = msg->in_reply_to.network_id;
    result[4]  = msg->in_reply_to.serial_num;
    result[5]  = msg->to;
    result[6]  = msg->from;
    result[7]  = msg->orig_from.network_id;
    result[8]  = msg->orig_from.local_id;
    result[9]  = msg->final_to.network_id;
    result[10] = msg->final_to.local_id;
    result[11] = msg->extra;                // to save adding it in the future
    result[12] = msg->flags;
    result[13] = msg->name_len;
    result[14] = msg->data_len;
    // There's no point in sending the name and data pointers - since we must
    // be sending an "entire" message, they must be NULL, and anyway they're
    // pointers...
    result[15] = msg->end_guard;

    for (ii=0; ii<KBUS_SERIALISED_HDR_LEN; ii++)
        result[ii] = htonl(result[ii]);
}

/*
 * Given a serialised message header from the network, set the message's header
 *
 * Leaves the message's name and data pointers unset (NULL).
 */
extern void kbus_unserialise_message_header(uint32_t        serial[KBUS_SERIALISED_HDR_LEN],
                                            kbus_message_t *msg)
{
    int ii;

    for (ii=0; ii<KBUS_SERIALISED_HDR_LEN; ii++)
        serial[ii] = ntohl(serial[ii]);

    msg->start_guard            = serial[0];
    msg->id.network_id          = serial[1];
    msg->id.serial_num          = serial[2];
    msg->in_reply_to.network_id = serial[3];
    msg->in_reply_to.serial_num = serial[4];
    msg->to                     = serial[5];
    msg->from                   = serial[6];
    msg->orig_from.network_id   = serial[7];
    msg->orig_from.local_id     = serial[8];
    msg->final_to.network_id    = serial[9];
    msg->final_to.local_id      = serial[10];
    msg->extra                  = serial[11]; // future proofing
    msg->flags                  = serial[12];
    msg->name_len               = serial[13];
    msg->data_len               = serial[14];
    msg->name                   = NULL;
    msg->data                   = NULL;
    msg->end_guard              = serial[15];
}

/*
 * Returns:
 *
 * * 0 if the message has successfully been amended.
 * * 1 if the message is not of interest and should be ignored.
 * * A negative number (``-errno``) for failure.
 */
static int amend_reply_from_socket(kbus_limpet_context_t  *context,
                                   kbus_message_t         *msg)
{
    uint32_t     from;

    // If this message is in reply to a message from our network,
    // revert to the original message id
    if (msg->in_reply_to.network_id == context->network_id)
        msg->in_reply_to.network_id = 0;

    // Look up the original Request, and amend appropriately
    from = find_request_from(context, msg->in_reply_to);
    if (from == 0) {
        // We couldn't find it - oh dear
        // Presumably we already dealt with this Reply once before
        if (context->verbosity > 1)
            printf("%u .. Ignoring this Reply as a 'listen' copy\n", context->network_id);
        return 1;
    }
    msg->to = from;
    // We don't want to preserve the network id - let KBUS give it
    // a whole new id
    msg->id.network_id = 0;
    msg->id.serial_num = 0;

    // And now we've dealt with it...
    (void) forget_request_from(context, msg->in_reply_to);

    if (context->verbosity > 1) {
        printf("%u .. amended Reply: ", context->network_id);
        kbus_msg_print(stdout, msg);
        printf("\n");
    }
    return 0;
}

/*
 * Returns:
 *
 * * 0 if the message has successfully been amended.
 * * 1 if the message is not of interest and should be ignored.
 * * 2 if an error occurred, and the 'error' message should be sent (back)
 *   to the other Limpet (in this case the original error should not be
 *   send to KBUS).
 * * A negative number (``-errno``) for failure.
 */
static int amend_request_from_socket(kbus_limpet_context_t   *context,
                                     kbus_message_t          *msg,
                                     kbus_message_t         **error)
{
    int         rv;
    bool        is_local;
    uint32_t    replier_id;

    *error = NULL;

    // The Request will have been marked as "to" our Limpet pair (otherwise
    // we would not have received it).
    //
    // If the "final_to" has a network id that matches ours, then we need to
    // unset that, as it has clearly now reached its "local" network
    if (msg->final_to.network_id == context->network_id) {
        msg->final_to.network_id = 0;       // Do we really need to do this?
        is_local = true;
    }
    else
        is_local = false;

    // Find out who KBUS thinks is replying to this message name
    rv = kbus_ksock_find_replier(context->ksock, msg->name, &replier_id);
    if (rv) {
        if (context->verbosity)
            printf("Limpet %u: Error finding replier for '%s': %u/%s\n",
                   context->network_id, msg->name, -rv, strerror(-rv));
        return rv;
    }

    if (replier_id == 0) {
        // Oh dear - there's no replier
        if (context->verbosity > 1)
            printf("%u .. Replier has gone away\n", context->network_id);
        rv = kbus_msg_create(error, KBUS_MSG_NAME_REPLIER_GONEAWAY,
                             strlen(KBUS_MSG_NAME_REPLIER_GONEAWAY),
                             NULL, 0, 0);
        if (rv) {
            if (context->verbosity)
                printf("Limpet %u: Unable to create (and send) ReplierGoneAway message\n",
                       context->network_id);
            return rv;
        }
        (*error)->to = msg->from;
        (*error)->in_reply_to = msg->id;
        printf("@@@ %u: rv = %d\n", context->network_id, rv);
        kbus_msg_print(stdout, msg);
        printf("\n");
        kbus_msg_print(stdout, *error);
        printf("\n");

        return 2;
    }

    if (context->verbosity > 1)
        printf("%u .. %s KBUS replier %u\n", context->network_id,
               is_local?"Local":"NonLocal", replier_id);

    if (is_local) {
        // The KBUS we're going to write the message to is the final KBUS.
        // Thus the replier id must match that of the original Replier.
        if (replier_id != msg->final_to.local_id) {
            // Oops - wrong replier - someone rebound
            if (context->verbosity)
                printf("Limpet %u: Replier is %u, wanted %u - not same Replier\n",
                       context->network_id, replier_id, msg->final_to.local_id);
            rv = kbus_msg_create(error, KBUS_MSG_NOT_SAME_KSOCK,
                                 strlen(KBUS_MSG_NOT_SAME_KSOCK),
                                 NULL, 0, 0);
            if (rv) {
                if (context->verbosity)
                    printf("Limpet %u: Unable to create (and send) ReplierNotSameKsock message\n",
                           context->network_id);
                return rv;
            }
            (*error)->to = msg->from;
            (*error)->in_reply_to = msg->id;
            printf("@@@ %u: rv = %d\n", context->network_id, rv);
            kbus_msg_print(stdout, msg);
            printf("\n");
            kbus_msg_print(stdout, *error);
            printf("\n");

            return 2;
        }
    }

    // Regardless, we believe the message is OK, so need to
    // adjust who it is meant to go to (locally)
    if (is_local) {
        // If we're in our final stage, then we insist that the
        // Replier we deliver to be the Replier we expected
        msg->to = msg->final_to.local_id;
    } else {
        // If we're just passing through, then just deliver it to
        // whoever is listening, on the assumption that they in turn
        // will pass it along, until it reaches its destination.
        // XXX What happens if they are not a Limpet?
        // XXX That would be bad - but I'm not sure how we could
        // XXX tell (short of allowing Limpets to register with
        // XXX KBUS, so that we can ask - and then a non-Limpet
        // XXX could presumably *pretend* to be a Limpet anyway)
        // XXX - a potentially infinite shell game then ensues...
        msg->to = replier_id;
    }

    if (context->verbosity > 1)
        printf("%u ..Adjusted the msg.to field\n", context->network_id);
    return 0;
}

static int setup_kbus(kbus_limpet_context_t *context,
                      char                  *message_name)
{
    int rv;

    // We only want to receive a single copy of any message from KBUS,
    // even if we had registered as (for instance) both Listener and Replier.
    rv = kbus_ksock_only_once(context->ksock, true);
    if (rv) {
        if (context->verbosity)
            printf("Limpet %u: Error requesting single message copies: %d/%s\n",
                   context->network_id, -rv, strerror(-rv));
        return rv;
    }

    // Bind to proxy the requested message name (presumably a wildcard)
    rv = kbus_ksock_bind(context->ksock, message_name, false);
    if (rv) {
        if (context->verbosity)
            printf("Limpet %u: Error binding as listener for '%s': %d/%s\n",
                   context->network_id, message_name, -rv, strerror(-rv));
        return rv;
    }

    // Specifically bind for Replier Bind Event messages - since we're only
    // getting single copies of messages, we don't mind if this overlaps
    // what we just did.
    rv = kbus_ksock_bind(context->ksock, KBUS_MSG_NAME_REPLIER_BIND_EVENT, false);
    if (rv) {
        if (context->verbosity)
            printf("Limpet %u: Error binding as listener for '%s': %d/%s\n",
                   context->network_id, KBUS_MSG_NAME_REPLIER_BIND_EVENT,
               -rv, strerror(-rv));
        return rv;
    }

    // And *ask* for Replier Bind Events to be issued
    rv = kbus_ksock_report_replier_binds(context->ksock, 1);
    if (rv) {
        if (context->verbosity)
            printf("Limpet %u: Error asking for Replier Bind Events: %d/%s\n",
                   context->network_id, -rv, strerror(-rv));
        return rv;
    }
    return 0;
}

/*
 * Prepare for Limper handling on the given Ksock, and return a Limpet context.
 *
 * This function binds to the requested message name, sets up Replier Bind
 * Event messages, and requests only one copy of each message.
 *
 * - 'ksock' is the Ksock which is to this end of our Limpet. It must be open
 *   for read and write.
 * - 'network_id' is the network id which identifies this Limpet. It is set in
 *   message ids when we are forwarding a message to the other Limpet. It must
 *   be greater than zero.
 * - 'other_network_id' is the network if of the other Limpet. It must not be
 *   the same as our_network_id. It must be greater than zero.
 * - 'message_name' is the message name that this Limpet will bind to, and
 *   forward. This will normally be a wildcard, and defaults to "$.*". Other
 *   messages will treated as ignorable. A copy is taken of this string.
 * - if 'verbosity' is:
 *
 *   * 0, we are as silent as possible
 *   * 1, we announce ourselves, and output any error/warning messages
 *   * 2 (or higher), we output information about each message as it is
 *     processed.
 *
 * 'context' is the Limpet context, allocated by this function. Free it with
 * ksock_limpet_free_context() when it is no longer required.
 *
 * Returns 0 if all goes well, a negative number (``-errno``) for
 * failure (in particular, returns -EBADMSG if 'message_name' is NULL or too
 * short).
 */
extern int kbus_limpet_new_context(kbus_ksock_t              ksock,
                                   uint32_t                  network_id,
                                   uint32_t                  other_network_id,
                                   char                     *message_name,
                                   uint32_t                  verbosity,
                                   kbus_limpet_context_t   **context)
{
    ssize_t  len;
    char    *name;
    int      rv;
    uint32_t ksock_id;

    kbus_limpet_context_t   *new;

    *context = NULL;

    rv = kbus_ksock_id(ksock, &ksock_id);
    if (rv < 0) {
        if (verbosity)
            printf("Limpet %u: Unable to determine Ksock id for kbus_limpet_new_context\n",
                   network_id);
        return rv;
    }

    if (!message_name) {
        if (verbosity)
            printf("Limpet %u: Message name is NULL for kbus_limpet_new_context\n",
                   network_id);
        return -EBADMSG;
    }
    len = strlen(message_name);
    if (len < 3) {
        if (verbosity)
            printf("Limpet %u: Message name '%s' is too short for"
                   " kbus_limpet_new_context\n",network_id, message_name);
        return -EBADMSG;
    }

    new = malloc(sizeof(*new));
    if (new == NULL) {
        return -ENOMEM;
    }

    name = malloc(len+1);
    if (name == NULL) {
        free(new);
        return -ENOMEM;
    }
    strcpy(name, message_name);

    new->ksock = ksock;
    new->ksock_id = ksock_id;
    new->message_name = name;
    new->network_id = network_id;
    new->other_network_id = other_network_id;
    new->verbosity = verbosity;

    new->replier_for = NULL;
    new->request_from = NULL;

    // And set up to do what we want
    rv = setup_kbus(new, message_name);
    if (rv) {
        free(new);
        free(name);
        return rv;
    }

    *context = new;

    return 0;
}

/*
 * Change the verbosity level for a Limpet context
 */
extern void kbus_limpet_set_verbosity(kbus_limpet_context_t *context,
                                      uint32_t               verbosity)
{
    context->verbosity = verbosity;
}

/*
 * Free a Kbus Limpet context that is no longer required.
 *
 * After freeing 'context', it will be set to a pointer to NULL.
 *
 * If 'context' is is already a pointer to NULL, this function does nothing.
 */
extern void kbus_limpet_free_context(kbus_limpet_context_t  **context)
{
    if (*context == NULL)
        return;

    if ((*context)->message_name) {
        free((*context)->message_name);
        (*context)->message_name = NULL;
    }

    forget_all_replier_for(*context);
    forget_all_request_from(*context);

    free(*context);
    *context = NULL;
}

/*
 * Given a message read from KBUS, amend it for sending to the other Limpet.
 *
 * Returns:
 *
 * * 0 if the message has successfully been amended, and should be sent to
 *   KBUS.
 * * 1 if the message is not of interest and should be ignored.
 * * A negative number (``-errno``) for failure.
 */
extern int kbus_limpet_amend_msg_from_kbus(kbus_limpet_context_t *context,
                                           kbus_message_t        *msg)
{
    int                  rv;
    char                *name = kbus_msg_name_ptr(msg);

    if (!strncmp(name, KBUS_MSG_NAME_REPLIER_BIND_EVENT, msg->name_len)) {

        void                            *data = kbus_msg_data_ptr(msg);
        kbus_replier_bind_event_data_t  *event;

        event = (kbus_replier_bind_event_data_t *)data;

        if (event->binder == context->ksock_id) {
            // This is the result of *us* binding as a proxy, so we don't
            // want to send it to the other limpet!
            if (context->verbosity > 1)
                printf("%u .. Ignoring our own [UN]BIND event\n",
                       context->network_id);
            return 1;
        }
    }

    if (kbus_msg_is_request(msg) && kbus_msg_wants_us_to_reply(msg)) {
        // Remember who this Request message was from, so that when we
        // get a Reply we can set *its* 'from' field correctly
        rv = remember_request_from(context, msg->id, msg->from);
        if (rv == -ENOMEM)
            return rv;
        else if (rv != 0)
            return 1;                   // what else to do?
        if (context->verbosity > 1)
            print_request_from(context);
    }

    if (msg->id.network_id == context->other_network_id) {
        // This is a message that originated with our pair Limpet (so it's
        // been from the other Limpet, to us, to KBUS, and we're now getting
        // it back again). Therefore we want to ignore it. When the original
        // message was sent to the other KBUS (before any Limpet touched
        // it), any listeners on that side would have heard it from that
        // KBUS, so we don't want to send it back to them yet again...
        if (context->verbosity > 1)
            printf("%u .. Ignoring message from other Limpet\n",
                   context->network_id);
        return 1;
    }

    // If KBUS gave us a message with an unset network id, then it is a local
    // message, and we set its network id to our own before we pass it on.
    // This combination of (network id, local id) should then be unique across
    // our whole network of Limpets and KBUSes.
    if (msg->id.network_id == 0)
        msg->id.network_id = context->network_id;

    // Limpets are responsible for setting the 'orig_from' field, which
    // indicates:
    //
    // 1. The ksock_id of the original sender of the message
    // 2. The network_id of the first Limpet to pass the message on to its
    //    pair.
    //
    // When the message gets *back* to this Limpet, we will be able to
    // recognise it (its network id will be the same as ours), and thus we will
    // know the ksock_id of its original sender, if we care.
    //
    // Moreover, we can use this information when setting up a stateful request
    // - the orig_from can be copied to the stateful request's final_to field,
    // the network/ksock we want to assert must handle the far end of the
    // dialogue.
    //
    // So, if we are the first Limpet to handle this message from KBUS, then we
    // give it our network id.
    if (msg->orig_from.network_id == 0) {
        msg->orig_from.network_id = context->network_id;
        msg->orig_from.local_id   = msg->from;
    }
    return 0;
}

/*
 * Given a message read from the other Limpet, amend it for sending to KBUS.
 *
 * * 'context' describes the Limpet environment
 * * 'msg' is the message to be amended. It will be changed appropriately.
 *   Note that the message data will never be touched.
 * * 'error' will be NULL or an error message to be sent to the other Limpet.
 *   In the latter case, it is up to the caller to free it.
 *
 * Returns:
 *
 * * 0 if the message has successfully been amended, and should be sent to
 *   KBUS.
 * * 1 if the message is not of interest and should be ignored.
 * * 2 if an error occurred, and the 'error' message should be sent (back)
 *   to the other Limpet (in this case the original error should not be
 *   send to KBUS).
 * * A negative number (``-errno``) for failure.
 */
extern int kbus_limpet_amend_msg_to_kbus(kbus_limpet_context_t  *context,
                                         kbus_message_t      *msg,
                                         kbus_message_t     **error)
{
    int              rv;
    char            *name = kbus_msg_name_ptr(msg);
    char            *bind_name = NULL;

    *error = NULL;

    if (context->verbosity > 1) {
        printf("%u Limpet->Us: ", context->network_id);
        kbus_msg_print(stdout, msg);
        printf("\n");
    }

    if (!strncmp(name, KBUS_MSG_NAME_REPLIER_BIND_EVENT, msg->name_len)) {
        // We have to bind/unbind as a Replier in proxy
        uint32_t     is_bind, binder;
        rv = kbus_msg_split_bind_event(msg, &is_bind, &binder, &bind_name);
        if (rv) return rv;

        if (is_bind) {
            if (context->verbosity > 1)
                printf("%u .. BIND '%s'\n", context->network_id, bind_name);
            rv = kbus_ksock_bind(context->ksock, bind_name, true);
            if (rv) {
                if (context->verbosity)
                    printf("Limpet %u: Error binding as replier to '%s': %u/%s\n",
                           context->network_id, bind_name, -rv, strerror(-rv));
                return rv;
            }
            rv = remember_replier_for(context, bind_name, binder);
            if (rv) {
                if (context->verbosity)
                    printf("Limpet %u: Error remembering replier for '%s'\n",
                           context->network_id, bind_name);
                if (rv == -ENOMEM)
                    return rv;
                else if (rv != 0)
                    return 1;       // what else can we do?
            }
            // The "replier for" datastructure is now using the name, so we
            // must take care not to free it...
            bind_name = NULL;
        } else {
            if (context->verbosity > 1)
                printf("%u .. UNBIND '%s'\n", context->network_id, bind_name);
            rv = kbus_ksock_unbind(context->ksock, bind_name, true);
            if (rv) {
                if (context->verbosity)
                    printf("Limpet %u: Error unbinding as replier to '%s': %u/%s\n",
                           context->network_id, bind_name, -rv, strerror(-rv));
                return rv;
            }
            rv = forget_replier_for(context, bind_name);
            if (rv) {
                if (context->verbosity)
                    printf("Limpet %u: Error forgetting replier for '%s'\n",
                           context->network_id, bind_name);
                // what to do? Ignore, maybe?
            }
        }
        print_replier_for(context);
        // Don't want to mirror this to KBUS
        return 1;
    }

    if (kbus_msg_is_reply(msg))
        rv = amend_reply_from_socket(context, msg);
    else if (kbus_msg_is_stateful_request(msg) &&
             kbus_msg_wants_us_to_reply(msg))
        rv = amend_request_from_socket(context, msg, error);
    else
        rv = 0;

    if (rv == 0 && context->verbosity > 1) {
        printf("%u Us->KBUS:   ", context->network_id);
        kbus_msg_print(stdout, msg);
        printf("\n");
    } else if (rv == 2 && context->verbosity > 1) {
        printf("%u Us->KBUS:   ", context->network_id);
        kbus_msg_print(stdout, *error);
        printf("\n");
    }
    return rv;
}

/*
 * Convert the data of a Replier Bind Event message to network order.
 *
 * Does not check the message name, so please only call it for
 * messages called "$.ReplierBindEvent" (KBUS_MSG_NAME_REPLIER_BIND_EVENT).
 */
extern void kbus_limpet_ReplierBindEvent_hton(kbus_message_t  *msg)
{
    void    *data = kbus_msg_data_ptr(msg);

    kbus_replier_bind_event_data_t  *event;

    event = (kbus_replier_bind_event_data_t *)data;
    event->is_bind  = htonl(event->is_bind);
    event->binder   = htonl(event->binder);
    event->name_len = htonl(event->name_len);
}

/*
 * Convert the data of a Replier Bind Event message to host order.
 *
 * Does not check the message name, so please only call it for
 * messages called "$.ReplierBindEvent" (KBUS_MSG_NAME_REPLIER_BIND_EVENT).
 */
extern void kbus_limpet_ReplierBindEvent_ntoh(kbus_message_t  *msg)
{
    void    *data = kbus_msg_data_ptr(msg);

    kbus_replier_bind_event_data_t  *event;

    event = (kbus_replier_bind_event_data_t *)data;
    event->is_bind  = ntohl(event->is_bind);
    event->binder   = ntohl(event->binder);
    event->name_len = ntohl(event->name_len);
}

/*
 * If sending to our Ksock failed, maybe generate a message suitable for
 * sending back to the other Limpet.
 *
 * 'msg' is the message we tried to send, 'errnum' is the error returned
 * by KBUS when it tried to send the message.
 *
 * 'error' is the new error message. The caller is responsible for freeing
 * 'error'.
 *
 * An 'error' message will be generated if the original message was a Request,
 * and (at time of writing) not otherwise.
 *
 * Returns
 *
 * * 0 if all goes well (in which case 'error' is non-NULL).
 * * 1 if there is no need to send an error to the other Limpet, 'error' is
 *   NULL, and the event should be ignored.
 * * A negative number (``-errno``) for failure.
 */
extern int kbus_limpet_could_not_send_to_kbus_msg(kbus_limpet_context_t  *context,
                                                  kbus_message_t      *msg,
                                                  int                  errnum,
                                                  kbus_message_t     **error)
{
    *error = NULL;

    // If we were trying to send a Request, we need to fake an
    // appropriate Reply
    if (kbus_msg_is_request(msg)) {
        int              rv;
        kbus_message_t  *errmsg;
        char             errfmt[] = "%s%u";
        char             errname[strlen(KBUS_MSG_REMOTE_ERROR_PREFIX)+11+1];
        sprintf(errname, errfmt, KBUS_MSG_REMOTE_ERROR_PREFIX, -errnum);
        rv = kbus_msg_create(&errmsg, errname, strlen(errname), NULL,0,0);
        if (rv) {
            if (context->verbosity)
                printf("Limpet %u: Cannot create $.KBUS.RemoteError Reply\n",
                       context->network_id);
            return rv;
        } else {
            errmsg->to = msg->from;
            errmsg->in_reply_to = msg->id;
            if (context->verbosity > 1) {
                printf("%u Us->KBUS:   ", context->network_id);
                kbus_msg_print(stdout, errmsg);
                printf("\n");
            }
            *error = errmsg;
            return 0;
        }
    }
    // XXX If we were sending a Reply, can we do anything useful?
    if (context->verbosity)
        printf("Limpet %u: send message error %d -- continuing\n",
               context->network_id, -errnum);
    return 1;
}


// Local Variables:
// tab-width: 8
// indent-tabs-mode: nil
// c-basic-offset: 4
// End:
// vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
