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
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Kynesim, Cambridge UK
 *   Richard Watts <rrw@kynesim.co.uk>
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


struct limpet_context {
    int              socket;             // Our connection to the other limpet
    kbus_ksock_t     ksock;              // Our connection to KBUS
    uint32_t         ksock_id;           // Our own ksock id
    uint32_t         network_id;         // Our network id
    uint32_t         other_network_id;   // The other limpet's network id
    char            *termination_message;// The message name that stops us
    replier_for_t   *replier_for;        // Message repliers
    request_from_t  *request_from;       // Requests we're expecting replies for
    int              verbosity;          // 0=quiet, 1=normal, 2=lots
};
typedef struct limpet_context limpet_context_t;

static void print_replier_for(limpet_context_t    *context)
{
    replier_for_t    *this = context->replier_for;
    while (this) {
        printf(".. %4u is replier for '%s'\n", this->binder, this->name);
        this = this->next;
    }
}

// Return the binder id, or 0 if we can't find one.
static uint32_t find_replier_for(limpet_context_t    *context,
                                 char                *name)
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

static int forget_replier_for(limpet_context_t  *context,
                              char              *name)
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
    printf("!!! Unable to find entry for replier binding '%s' to delete\n",
           name);
    return -1;
}

// If we succeed, then we "own" the name, and the caller should not free it
static int remember_replier_for(limpet_context_t    *context,
                                char                *name,
                                uint32_t             binder)
{
    replier_for_t    *new = NULL;

    if (find_replier_for(context, name)) {
        // We decide that it's an error to already have an entry
        printf("### Attempt to remember another binder for '%s'\n",name);
        return -1;
    }

    new = malloc(sizeof(*new));
    if (!new) {
        printf("### Cannot allocate memory for remembering a binding\n");
        return -1;
    }

    new->name = name;
    new->binder = binder;
    new->next = context->replier_for;

    context->replier_for = new;

    return 0;
}

static void forget_all_replier_for(limpet_context_t *context)
{
    replier_for_t   *ptr = context->replier_for;
    while (ptr) {
        replier_for_t   *next = ptr->next;
        free(ptr->name);
        free(ptr);
        ptr = next;
    }
}

static void print_request_from(limpet_context_t    *context)
{
    request_from_t    *this = context->request_from;
    while (this) {
        printf(".. message [%u:%u] was from %u\n", this->id.network_id,
               this->id.serial_num, this->from);
        this = this->next;
    }
}

// Return the 'from' id, or 0 if we can't find one.
static uint32_t find_request_from(limpet_context_t    *context,
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

static int forget_request_from(limpet_context_t  *context,
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
    printf("!!! Unable to find entry for request from [%u:%u] to delete\n",
           id.network_id, id.serial_num);
    return -1;
}

static int remember_request_from(limpet_context_t    *context,
                                 kbus_msg_id_t        id,
                                 uint32_t             from)
{
    request_from_t    *new = NULL;

    if (find_request_from(context, id)) {
        // We decide that it's an error to already have an entry
        printf("### Attempt to remember another request 'from' for [%u:%u]\n",
               id.network_id, id.serial_num);
        return -1;
    }

    new = malloc(sizeof(*new));
    if (!new) {
        printf("### Cannot allocate memory for remembering a binding\n");
        return -1;
    }

    new->id = id;
    new->from = from;
    new->next = context->request_from;

    context->request_from = new;

    return 0;
}

static void forget_all_request_from(limpet_context_t *context)
{
    request_from_t   *ptr = context->request_from;
    while (ptr) {
        request_from_t   *next = ptr->next;
        free(ptr);
        ptr = next;
    }
}

// Length of an array sufficient to hold the parts of a message header that
// we need to send over the network
#define KBUS_SERIALISED_HDR_LEN 16

// Given a KBUS message, set the `result` array to its content, suitable for
// sending across the network
static void serialise_message_header(kbus_message_t      *msg,
                                     uint32_t             result[KBUS_SERIALISED_HDR_LEN])
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

// Given a serialised message header from the network, set the message's header
static void unserialise_message_header(uint32_t             serial[KBUS_SERIALISED_HDR_LEN],
                                       kbus_message_t      *msg)
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

static int send_message_to_other_limpet(limpet_context_t  *context,
                                        kbus_message_t    *msg)
{
    int         rv;
    uint32_t    array[KBUS_SERIALISED_HDR_LEN];
    char       *name = NULL;
    void       *data = NULL;

    uint32_t    padded_name_len;
    uint32_t    padded_data_len;

    static char padding[] = "\0\0\0\0\0\0\0\0";

    name = kbus_msg_name_ptr(msg);
    data = kbus_msg_data_ptr(msg);

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

    // And, since we're going to throw it onto the network...
    serialise_message_header(msg, array);

    rv = send(context->socket, array, sizeof(array), 0);
    if (rv < 0) {
        printf("### Error sending message header to other limpet: %s\n",
               strerror(errno));
        return -1;
    }

    padded_name_len = KBUS_PADDED_NAME_LEN(msg->name_len);
    rv = send(context->socket, name, msg->name_len, 0);
    if (rv < 0) {
        printf("### Error sending message name to other limpet: %s\n",
               strerror(errno));
        return -1;
    }
    if (padded_name_len - msg->name_len > 0) {
        rv = send(context->socket, padding, padded_name_len-msg->name_len, 0);
        if (rv < 0) {
            printf("### Error sending message name padding to other limpet: %s\n",
                   strerror(errno));
            return -1;
        }
    }

    if (msg->data_len != 0 && data != NULL) {

        // We know the structure of Replier Bind Event data, and can mangle
        // it appropriately for the network
        if (!strncmp(name, KBUS_MSG_NAME_REPLIER_BIND_EVENT, msg->name_len)) {

            kbus_replier_bind_event_data_t  *event;

            event = (kbus_replier_bind_event_data_t *)data;
            event->is_bind  = htonl(event->is_bind);
            event->binder   = htonl(event->binder);
            event->name_len = htonl(event->name_len);
        }

        padded_data_len = KBUS_PADDED_DATA_LEN(msg->data_len);
        rv = send(context->socket, data, msg->data_len, 0);
        if (rv < 0) {
            printf("### Error sending message data to other limpet: %s\n",
                   strerror(errno));
            return -1;
        }
        if (padded_data_len - msg->data_len > 0) {
            rv = send(context->socket, padding, padded_data_len-msg->data_len, 0);
            if (rv < 0) {
                printf("### Error sending message data padding to other limpet: %s\n",
                       strerror(errno));
                return -1;
            }
        }
    }

    // And a final end guard for safety
    rv = send(context->socket, &array[KBUS_SERIALISED_HDR_LEN-1], sizeof(uint32_t), 0);
    if (rv < 0) {
        printf("### Error sending final message end guard to other limpet: %s\n",
               strerror(errno));
        return -1;
    }

    return 0;
}

static int handle_message_from_kbus(limpet_context_t    *context,
                                    bool                *terminate)
{
    int                  rv;
    kbus_message_t      *msg;
    char                *name;
    
    *terminate = false;

    rv = kbus_ksock_read_next_msg(context->ksock, &msg);
    if (rv < 0) {
        printf("### Error reading message from KBUS: %d/%s\n",-rv,strerror(-rv));
        return rv;
    }

    printf("%u KBUS->Us:   ", context->network_id);
    kbus_msg_print(stdout, msg);
    printf("\n");

    name = kbus_msg_name_ptr(msg);

    if (context->termination_message != NULL &&
        !strncmp(context->termination_message, name, msg->name_len)) {
        kbus_msg_delete(&msg);
        *terminate = true;
        return 0;
    }

    if (!strncmp(name, KBUS_MSG_NAME_REPLIER_BIND_EVENT, msg->name_len)) {

        void                            *data = kbus_msg_data_ptr(msg);
        kbus_replier_bind_event_data_t  *event;
        event = (kbus_replier_bind_event_data_t *)data;

        if (event->binder == context->ksock_id) {
            // This is the result of *us* binding as a proxy, so we don't
            // want to send it to the other limpet!
            printf(".. Ignoring our own [UN]BIND event\n");
            kbus_msg_delete(&msg);
            return 0;
        }
    }

    if (kbus_msg_is_request(msg) && kbus_msg_wants_us_to_reply(msg)) {
        // Remember who this Request message was from, so that when we
        // get a Reply we can set *its* 'from' field correctly
        rv = remember_request_from(context, msg->id, msg->from);
        if (rv) {                       // what else can we do...
            kbus_msg_delete(&msg);
            return rv;
        }
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
        printf(".. Ignoring message from other Limpet\n");
        kbus_msg_delete(&msg);
        return 0;
    }

    rv = send_message_to_other_limpet(context, msg);

    printf("%u Us->Limpet: ",context->network_id);
    kbus_msg_print(stdout, msg);
    printf("\n");

    kbus_msg_delete(&msg);
    return rv;
}

static int read_message_from_other_limpet(limpet_context_t     *context,
                                          kbus_message_t      **msg)
{
    uint32_t    array[KBUS_SERIALISED_HDR_LEN];
    ssize_t     wanted = sizeof(array);
    ssize_t     length;
    uint32_t    final_end_guard;

    kbus_message_t      *new_msg = NULL;
    char                *name = NULL;
    void                *data = NULL;

    uint32_t             padded_name_len;
    uint32_t             padded_data_len;

    length = recv(context->socket, array, wanted, MSG_WAITALL);
    if (length == 0) {
        printf("!!! Trying to read message: other Limpet has gone away\n");
        goto error_return;
    } else if (length != wanted) {
        printf("### Unable to read whole message header from other Limpet: %s\n",
               strerror(errno));
        goto error_return;
    }

    new_msg = malloc(sizeof(*new_msg));
    if (new_msg == NULL) {
        printf("### Unable to allocate message header\n");
        goto error_return;
    }

    unserialise_message_header(array, new_msg);

    if (new_msg->start_guard != KBUS_MSG_START_GUARD) {
        printf("Message start guard from other limpet is %08x, not %08x\n",
               new_msg->start_guard, KBUS_MSG_START_GUARD);
        goto error_return;
    } else if (new_msg->end_guard != KBUS_MSG_END_GUARD) {
        printf("Message end guard from other limpet is %08x, not %08x\n",
               new_msg->end_guard, KBUS_MSG_END_GUARD);
        goto error_return;
    }

    // Note that the name, as sent, was padded with zero bytes at the end
    // We *could* read the name and then ignore some bytes, but it's simpler
    // just to read (and remember) the extra data and quietly ignore it
    // Remember that this padding *includes* guaranteed zero termination byte
    // for the string, so we don't need to add one in to the length
    padded_name_len = KBUS_PADDED_NAME_LEN(new_msg->name_len);
    name = malloc(padded_name_len);
    if (name == NULL) {
        printf("### Unable to allocate message name\n");
        goto error_return;
    }
    length = recv(context->socket, name, padded_name_len, MSG_WAITALL);
    if (length == 0) {
        printf("!!! Trying to read message name: other Limpet has gone away\n");
        goto error_return;
    } else if (length != padded_name_len) {
        printf("### Unable to read whole message name from other Limpet: %s\n",
               strerror(errno));
        goto error_return;
    }
    name[new_msg->name_len] = 0;    // This *should not* be needed, but heh...
    new_msg->name = name;

    if (new_msg->data_len) {
        // Similar comments for the data and the number of bytes transmitted
        padded_data_len = KBUS_PADDED_DATA_LEN(new_msg->data_len);
        data = malloc(padded_data_len);
        if (name == NULL) {
            printf("### Unable to allocate message data\n");
            goto error_return;
        }
        length = recv(context->socket, data, padded_data_len, MSG_WAITALL);
        if (length == 0) {
            printf("!!! Trying to read message data: other Limpet has gone away\n");
            goto error_return;
        } else if (length != padded_data_len) {
            printf("### Unable to read whole message data from other Limpet: %s\n",
                   strerror(errno));
            goto error_return;
        }

        // We know the structure of Replier Bind Event data, and can mangle
        // it appropriately for having come from the network
        if (!strncmp(name, KBUS_MSG_NAME_REPLIER_BIND_EVENT, new_msg->name_len)) {

            kbus_replier_bind_event_data_t  *event;

            event = (kbus_replier_bind_event_data_t *)data;
            event->is_bind  = ntohl(event->is_bind);
            event->binder   = ntohl(event->binder);
            event->name_len = ntohl(event->name_len);
        }

        new_msg->data = data;
    }

    // And read a final end guard
    wanted = sizeof(uint32_t);              // erm, 4...
    length = recv(context->socket, &final_end_guard, wanted, MSG_WAITALL);
    if (length == 0) {
        printf("!!! Trying to read message end guard: other Limpet has gone away\n");
        goto error_return;
    } else if (length != wanted) {
        printf("### Unable to read message end guard from other Limpet: %s\n",
               strerror(errno));
        goto error_return;
    }
    final_end_guard = ntohl(final_end_guard);
    if (final_end_guard != KBUS_MSG_END_GUARD) {
        printf("### Message final end guard from other limpet is %08x, not %08x\n",
               final_end_guard, KBUS_MSG_END_GUARD);
        goto error_return;
    }

    *msg = new_msg;
    return 0;

error_return:
    if (new_msg)  free(new_msg);
    if (name) free(name);
    if (data) free(data);
    return -1;
}

static int amend_reply_from_socket(limpet_context_t     *context,
                                   kbus_message_t       *msg,
                                   bool                 *ignore)
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
        printf("Ignoring this Reply as a 'listen' copy\n");
        *ignore = true;
        return 0;
    }
    msg->to = from;
    // We don't want to preserve the network id - let KBUS give it
    // a whole new id
    msg->id.network_id = 0;
    msg->id.serial_num = 0;

    // And now we've dealt with it...
    (void) forget_request_from(context, msg->in_reply_to);

    printf(".. amended Reply: ");
    kbus_msg_print(stdout, msg);
    printf("\n");

    *ignore = false;
    return 0;
}

static int amend_request_from_socket(limpet_context_t   *context,
                                     kbus_message_t     *msg,
                                     bool               *ignore)
{
    int         rv;
    bool        is_local;
    uint32_t    replier_id;

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
        printf("### Error finding replier for '%s': %u/%s\n", msg->name,
               -rv, strerror(-rv));
        return -1;
    }

    if (replier_id == 0) {
        kbus_message_t      *error;
        // Oh dear - there's no replier
        printf(".. Replier has gone away\n");
        rv = kbus_msg_create(&error, KBUS_MSG_NAME_REPLIER_GONEAWAY,
                             strlen(KBUS_MSG_NAME_REPLIER_GONEAWAY),
                             NULL, 0, 0);
        if (rv) {
            printf("XXX Unable to create (and send) ReplierGoneAway message\n");
            return -1;
        }
        error->to = msg->from;
        error->in_reply_to = msg->id;

        printf("%u Us->KBUS:   ",context->network_id);
        kbus_msg_print(stdout, error);
        printf("\n");

        rv = send_message_to_other_limpet(context, error);
        if (rv) {
            printf("XXX Unable to send ReplierGoneAway message\n");
            return -1;
        }
        *ignore = true;
        return 0;
    }

    printf(".. %s KBUS replier %u\n", is_local?"Local":"NonLocal", replier_id);

    if (is_local) {
        // The KBUS we're going to write the message to is the final KBUS.
        // Thus the replier id must match that of the original Replier.
        if (replier_id != msg->final_to.local_id) {
            kbus_message_t      *error;
            char                 errname[] = "$.KBUS.Replier.NotSameKsock";
            // Oops - wrong replier - someone rebound
            printf("XXX Replier is %u, wanted %u - Replier gone away\n",
                   replier_id, msg->final_to.local_id);
            rv = kbus_msg_create(&error, errname, strlen(errname),
                                 NULL, 0, 0);
            if (rv) {
                printf("XXX Unable to create (and send) ReplierNotSameKsock message\n");
                return -1;
            }
            error->to = msg->from;
            error->in_reply_to = msg->id;

            printf("%u Us->KBUS:   ",context->network_id);
            kbus_msg_print(stdout, error);
            printf("\n");

            rv = send_message_to_other_limpet(context, error);
            if (rv) {
                printf("XXX Unable to send Replier.NotSameKsock message\n");
                return -1;
            }
            *ignore = true;
            return 0;
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

    printf("..Adjusted the msg.to field\n");

    *ignore = false;
    return 0;
}

static int handle_message_from_other_limpet(limpet_context_t    *context)
{
    int              rv;
    kbus_message_t  *msg = NULL;
    char            *bind_name = NULL;
    kbus_msg_id_t    msg_id;
    bool             ignore = false;

    rv = read_message_from_other_limpet(context, &msg);
    if (rv) return -1;

    printf("%u Limpet->Us: ", context->network_id);
    kbus_msg_print(stdout, msg);
    printf("\n");

    if (!strncmp(msg->name, KBUS_MSG_NAME_REPLIER_BIND_EVENT, msg->name_len)) {
        // We have to bind/unbind as a Replier in proxy
        uint32_t     is_bind, binder;
        rv = kbus_msg_split_bind_event(msg, &is_bind, &binder, &bind_name);
        if (rv) goto tidyup;

        if (is_bind) {
            printf("%u BIND '%s'\n", context->network_id, bind_name);
            rv = kbus_ksock_bind(context->ksock, bind_name, true);
            if (rv) {
                printf("### Error binding as replier to '%s': %u/%s\n",
                       bind_name, -rv, strerror(-rv));
                goto tidyup;
            }
            rv = remember_replier_for(context, bind_name, binder);
            if (rv) {
                printf("### Error remembering replier for '%s'\n", bind_name);
                goto tidyup;
            }
            // The "replier for" datastructure is now using the name, so we
            // must take care not to free it...
            bind_name = NULL;
        } else {
            printf("%u UNBIND '%s'\n", context->network_id, bind_name);
            rv = kbus_ksock_unbind(context->ksock, bind_name, true);
            if (rv) {
                printf("### Error unbinding as replier to '%s': %u/%s\n",
                       bind_name, -rv, strerror(-rv));
                goto tidyup;
            }
            rv = forget_replier_for(context, bind_name);
            if (rv) {
                printf("### Error forgetting replier for '%s'\n", bind_name);
                goto tidyup;
            }
        }
        print_replier_for(context);
        // Don't want to mirror this to KBUS
        goto tidyup;
    }

    if (kbus_msg_is_reply(msg))
        rv = amend_reply_from_socket(context, msg, &ignore);
    else if (kbus_msg_is_stateful_request(msg) &&
               kbus_msg_wants_us_to_reply(msg))
        rv = amend_request_from_socket(context, msg, &ignore);

    if (rv) goto tidyup;
    if (ignore) {
        goto tidyup;    // No need to send a message
    }

    printf("%u Us->KBUS:   ", context->network_id);
    kbus_msg_print(stdout, msg);
    printf("\n");

    rv = kbus_ksock_send_msg(context->ksock, msg, &msg_id);
    if (rv) {
        // If we were trying to send a Request, we need to fake an
        // appropriate Reply
        if (kbus_msg_is_request(msg)) {
            kbus_message_t  *errmsg;
            char             errfmt[] = "$.KBUS.RemoteError.%u";
            char             errname[19+11+1];
            sprintf(errname, errfmt, -rv);
            rv = kbus_msg_create(&errmsg, errname, strlen(errname), NULL,0,0);
            if (rv) {
                printf("XXX Cannot create $.KBUS.RemoteError Reply\n");
                goto tidyup;
            } else {
                errmsg->to = msg->from;
                errmsg->in_reply_to = msg->id;
                printf("%u Us->KBUS:   ", context->network_id);
                kbus_msg_print(stdout, errmsg);
                printf("\n");
                rv = kbus_ksock_send_msg(context->ksock, errmsg, &msg_id);
                free(errmsg);
                if (rv) {
                    printf("XXX Cannot send $.KBUS.RemoteError Reply\n");
                    goto tidyup;
                }
            }
        }
        // XXX If we were sending a Reply, can we do anything useful?
        printf(".. send message error %u -- continuing\n",-rv);
    }

    rv = 0;

tidyup:
    if (bind_name) free(bind_name);
    if (msg) {
        if (msg->name) free(msg->name);
        if (msg->data) free(msg->data);
        free(msg);
    }
    return rv;
}

/*
 * Read the other limpet's network id.
 */
static int read_network_id(int       limpet_socket,
                           uint32_t *network_id)
{
    char        hello[5];
    uint32_t    value = 0;
    ssize_t     length;

    length = recv(limpet_socket, hello, 4, MSG_WAITALL);
    if (length != 4) {             // perhaps a little optimistic...
        printf("### Unable to read 'HELO' from other Limpet: %s\n",strerror(errno));
        return -1;
    }

    if (strncmp("HELO", hello, 4)) {
        printf("### Read '%.4s' from other Limpet, instead of 'HELO'\n",hello);
        return -1;
    }

    length = recv(limpet_socket, &value, 4, MSG_WAITALL);
    if (length != 4) {
        printf("### Unable to read network id from other Limpet: %s\n",strerror(errno));
        return -1;
    }

    *network_id = ntohl(value);

    return 0;
}

/*
 * Send the other limpet our network id.
 */
static int send_network_id(int      limpet_socket,
                           uint32_t network_id)
{
    char        *hello = "HELO";
    uint32_t     value = htonl(network_id);
    ssize_t      written;

    written = send(limpet_socket, hello, 4, 0);
    if (written != 4) {             // perhaps a little optimistic...
        printf("### Unable to write 'HELO' to other Limpet: %s\n",strerror(errno));
        return -1;
    }

    written = send(limpet_socket, &value, 4, 0);
    if (written != 4) {
        printf("### Unable to write network id to other Limpet: %s\n",strerror(errno));
        return -1;
    }

    return 0;
}

static int setup_kbus(kbus_ksock_t       ksock,
                      char              *message_name)
{
    int rv;

    // We only want to receive a single copy of any message from KBUS,
    // even if we had registered as (for instance) both Listener and Replier.
    rv = kbus_ksock_only_once(ksock, true);
    if (rv) {
        printf("### Error requesting single message copies: %d/%s\n",
               -rv, strerror(-rv));
        return rv;
    }

    // Bind to proxy the requested message name (presumably a wildcard)
    rv = kbus_ksock_bind(ksock, message_name, false);
    if (rv) {
        printf("### Error binding as listener for '%s': %d/%s\n",
               message_name, -rv, strerror(-rv));
        return rv;
    }

    // Specifically bind for Replier Bind Event messages - since we're only
    // getting single copies of messages, we don't mind if this overlaps
    // what we just did.
    rv = kbus_ksock_bind(ksock, KBUS_MSG_NAME_REPLIER_BIND_EVENT, false);
    if (rv) {
        printf("### Error binding as listener for '%s': %d/%s\n",
               KBUS_MSG_NAME_REPLIER_BIND_EVENT, -rv, strerror(-rv));
        return rv;
    }

    // And *ask* for Replier Bind Events to be issued
    rv = kbus_ksock_report_replier_binds(ksock, 1);
    if (rv) {
        printf("### Error asking for Replier Bind Events: %d/%s\n",
               -rv, strerror(-rv));
        return rv;
    }
    return 0;
}

/*
 * Run a KBUS Limpet.
 *
 * A Limpet proxies KBUS messages to/from another Limpet.
 *
 * `ksock` is the Ksock to use to communicate with KBUS. It must have been
 * opened for read and write.
 *
 * `limpet_socket` is the socket to use to communicate with the other Limpet of
 * this pair.
 *
 * `network_id` is a positive, non-negative integer identifying this Limpet.
 * All Limpets that can rech each other (i.e., by passing messages via other
 * Limpets and other KBUS devives) must have distinct network ids. This Limpet
 * will check that it has a different network id than its pair.
 *
 * `message_name` is what this Limpet will "listen" to -- all messages matching
 * this will be forwarded to the other Limpet. If it is NULL, then "$.*" will
 * be used. Note that the Limpet will also listen for Replier Bind Events (and
 * act on them).
 *
 * If `termination_message` is non-NULL, then this Limpet will exit when it read
 * a message with that name from KBUS.
 *
 * `verbosity` determines how much information a Limpet will write to standard
 * output. 0 means to be quiet, 1 means a moderate amount of output, 2 will
 * produce messages giving details of exactly how messages are being received,
 * sent and manipulated.
 *
 * This function is not normally expected to return, but given that, it returns
 * 0 if `termination_message` was given, and the Limpet received such a
 * message, or -1 if it went wrong.
 */
extern int kbus_limpet(kbus_ksock_t     ksock,
                       int              limpet_socket,
                       uint32_t         network_id,
                       char            *message_name,
                       char            *termination_message,
                       int              verbosity)
{
    int             rv = 0;
    uint32_t        other_network_id;
    uint32_t        ksock_id;
    struct pollfd   fds[2];

    limpet_context_t    context = {0};

    if (network_id < 1) {
        printf("### Limpet network id must be > 0, not %d\n",network_id);
        return -1;
    }

    if (message_name == NULL) {
      if (verbosity > 1)
        printf("Limpet defaulting to proxy messages matching '$.*'\n");
      message_name = "$.*";
    }

    printf("Sending our network id, %u\n", network_id);
    rv = send_network_id(limpet_socket, network_id);
    if (rv) goto tidyup;

    printf("Reading the other limpet's network id\n");
    rv = read_network_id(limpet_socket, &other_network_id);
    if (rv) goto tidyup;

    printf("The other limpet's network id is %u\n", other_network_id);

    if (other_network_id == network_id) {
        printf("### This Limpet and its pair both have network id %u\n",
               network_id);
        rv = -1;
        goto tidyup;
    }

    rv = setup_kbus(ksock, message_name);
    if (rv) goto tidyup;
    
    rv = kbus_ksock_id(ksock, &ksock_id);
    if (rv) goto tidyup;

    context.socket = limpet_socket;
    context.ksock = ksock;
    context.network_id = network_id;
    context.other_network_id = other_network_id;
    context.termination_message = termination_message;
    context.replier_for = NULL;
    context.ksock_id = ksock_id;
    context.verbosity = verbosity;

    printf("...and do stuff\n");

    fds[0].fd = ksock;
    fds[0].events = POLLIN; // We want to read a KBUS message
    fds[1].fd = limpet_socket;
    fds[1].events = POLLIN; // We want to read a message from our pair
    for (;;) {
        int results;
        fds[0].revents = 0;
        fds[1].revents = 0;
        results = poll(fds, 2, -1);     // No timeout, we're patient
        if (results < 0) {
            printf("### Waiting for messages abandoned: %s\n",strerror(errno));
            goto tidyup;
        }

        printf("\n");

        if (fds[0].revents & POLLIN) {
            bool  terminate;
            printf("%u ----------------- Message from KBUS\n", network_id);
            rv = handle_message_from_kbus(&context, &terminate);
            if (rv < 0 || terminate) goto tidyup;
        }

        if (fds[1].revents & POLLIN) {
            printf("%u ----------------- Message from other Limpet\n", network_id);
            rv = handle_message_from_other_limpet(&context);
            if (rv < 0) goto tidyup;
        }
    }

tidyup:

    if (context.replier_for)
        forget_all_replier_for(&context);

    if (context.request_from)
        forget_all_request_from(&context);

    return rv;
}

// Local Variables:
// tab-width: 8
// indent-tabs-mode: nil
// c-basic-offset: 4
// End:
// vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
