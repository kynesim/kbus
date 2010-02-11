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

struct limpet_context {
    int              socket;             // Connection to the other limpet
    kbus_ksock_t     ksock;              // Connection to KBUS
    uint32_t         network_id;         // Our network id
    uint32_t         other_network_id;   // The other limpet's network id
    char            *termination_message;// The message name that stops us
};
typedef struct limpet_context limpet_context_t;

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

    name = kbus_msg_name_ptr(msg);
    data = kbus_msg_data_ptr(msg);

    serialise_message_header(msg, array);

    rv = send(context->socket, array, sizeof(array), 0);
    if (rv < 0) {
        printf("### Error sending message header to other limpet: %s\n",
               strerror(errno));
        return -1;
    }

    rv = send(context->socket, name, msg->name_len, 0);
    if (rv < 0) {
        printf("### Error sending message name to other limpet: %s\n",
               strerror(errno));
        return -1;
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

        rv = send(context->socket, data, msg->data_len, 0);
        if (rv < 0) {
            printf("### Error sending message data to other limpet: %s\n",
                   strerror(errno));
            return -1;
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

static int handle_message_from_kbus(limpet_context_t    *context)
{
    int                  rv;
    kbus_message_t      *msg;
    
    rv = kbus_ksock_read_next_msg(context->ksock, &msg);
    if (rv < 0) {
        printf("### Error reading message from KBUS: %d/%s\n",-rv,strerror(-rv));
        return rv;
    }

    printf("KBUS->Us: ");
    kbus_msg_print(stdout, msg);
    printf("\n");

    rv = send_message_to_other_limpet(context, msg);

    kbus_msg_delete(&msg);
    return rv;
}

static int handle_message_from_other_limpet(limpet_context_t    *context)
{
    int         rv;
    uint32_t    array[KBUS_SERIALISED_HDR_LEN];
    ssize_t     wanted = sizeof(array);
    ssize_t     length;
    uint32_t    final_end_guard;

    kbus_message_t      *msg = NULL;
    char                *name = NULL;
    void                *data = NULL;

    length = recv(context->socket, array, wanted, MSG_WAITALL);
    if (length == 0) {
        printf("!!! Trying to read message: other Limpet has gone away\n");
        rv = -1;
        goto tidyup;
    } else if (length != wanted) {
        printf("### Unable to read whole message header from other Limpet: %s\n",
               strerror(errno));
        rv = -1;
        goto tidyup;
    }

    msg = malloc(sizeof(*msg));
    if (msg == NULL) {
        printf("### Unable to allocate message header\n");
        return -1;
    }

    unserialise_message_header(array, msg);

    if (msg->start_guard != KBUS_MSG_START_GUARD) {
        printf("Message start guard from other limpet is %08x, not %08x\n",
               msg->start_guard, KBUS_MSG_START_GUARD);
        rv = -1;
        goto tidyup;
    } else if (msg->end_guard != KBUS_MSG_END_GUARD) {
        printf("Message end guard from other limpet is %08x, not %08x\n",
               msg->end_guard, KBUS_MSG_END_GUARD);
        rv = -1;
        goto tidyup;
    }

    name = malloc(msg->name_len + 1);
    if (name == NULL) {
        printf("### Unable to allocate message name\n");
        rv = -1;
        goto tidyup;
    }
    length = recv(context->socket, name, msg->name_len, MSG_WAITALL);
    if (length == 0) {
        printf("!!! Trying to read message name: other Limpet has gone away\n");
        rv = -1;
        goto tidyup;
    } else if (length != msg->name_len) {
        printf("### Unable to read whole message name from other Limpet: %s\n",
               strerror(errno));
        rv = -1;
        goto tidyup;
    }
    name[msg->name_len] = 0;
    msg->name = name;

    if (msg->data_len) {
        data = malloc(msg->data_len);
        if (name == NULL) {
            printf("### Unable to allocate message data\n");
            rv = -1;
            goto tidyup;
        }
        length = recv(context->socket, data, msg->data_len, MSG_WAITALL);
        if (length == 0) {
            printf("!!! Trying to read message data: other Limpet has gone away\n");
            rv = -1;
            goto tidyup;
        } else if (length != msg->data_len) {
            printf("### Unable to read whole message data from other Limpet: %s\n",
                   strerror(errno));
            rv = -1;
            goto tidyup;
        }

        // We know the structure of Replier Bind Event data, and can mangle
        // it appropriately for having come from the network
        if (!strncmp(name, KBUS_MSG_NAME_REPLIER_BIND_EVENT, msg->name_len)) {

            kbus_replier_bind_event_data_t  *event;

            event = (kbus_replier_bind_event_data_t *)data;
            event->is_bind  = ntohl(event->is_bind);
            event->binder   = ntohl(event->binder);
            event->name_len = ntohl(event->name_len);
        }

        msg->data = data;
    }

    // And read a final end guard
    wanted = sizeof(uint32_t);              // erm, 4...
    length = recv(context->socket, &final_end_guard, wanted, MSG_WAITALL);
    if (length == 0) {
        printf("!!! Trying to read message end guard: other Limpet has gone away\n");
        rv = -1;
        goto tidyup;
    } else if (length != wanted) {
        printf("### Unable to read message end guard from other Limpet: %s\n",
               strerror(errno));
        rv = -1;
        goto tidyup;
    }
    final_end_guard = ntohl(final_end_guard);
    if (final_end_guard != KBUS_MSG_END_GUARD) {
        printf("### Message final end guard from other limpet is %08x, not %08x\n",
               final_end_guard, KBUS_MSG_END_GUARD);
        rv = -1;
        goto tidyup;
    }

    printf("Limpet->Us: ");
    kbus_msg_print(stdout, msg);
    printf("\n");

    // And, notionally, do something with it...
    //
    //
    rv = 0;

tidyup:
    if (msg)  free(msg);
    if (name) free(name);
    if (data) free(data);
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

static int open_client_socket(char     *address,
                              int       port,
                              int      *client_socket)
{
  int    err;
  int    family;
  int    opt[1];

  if (port == 0)
      family = AF_UNIX;
  else
      family = AF_INET;

  *client_socket = socket(family, SOCK_STREAM, 0);
  if (*client_socket == -1)
  {
    fprintf(stderr,"### Unable to create socket: %s\n",strerror(errno));
    return -1;
  }

  // Try to allow address reuse as soon as possible after we've finished
  // with it
  opt[0] = 1;
  (void) setsockopt(*client_socket, SOL_SOCKET, SO_REUSEADDR, opt, sizeof(int));

  if (family == AF_UNIX)
  {
      struct sockaddr_un  unaddr = {0};
      socklen_t           length;
      unaddr.sun_family = AF_UNIX;
      if (strlen(address) > 104)                // apparently 104 is the limit
          printf("!!! Address '%s' is too long, truncated\n",address);
      strncpy(unaddr.sun_path, address, 104);   // yes, really, 104
      unaddr.sun_path[104] = 0;                 // that number again
      // If the file with that name already exists, then we will fail.
      // But I'd rather not delete the file if we've not created it
      // - that would be something of a Bad Thing to do to someone!
      length = sizeof(unaddr.sun_family) + strlen(unaddr.sun_path);

      err = connect(*client_socket, (struct sockaddr *)&unaddr, length);
      if (err == -1)
      {
        fprintf(stderr,"### Unable to connect to %s: %s\n",
                address,strerror(errno));
        return -1;
      }
      printf("Connected  to %s on socket %d\n",address,*client_socket);
  }
  else
  {
      struct hostent     *hp;
      struct sockaddr_in  ipaddr;
      hp = gethostbyname(address);
      if (hp == NULL)
      {
          fprintf(stderr,"### Unable to resolve host %s: %s\n",
                  address,strerror(h_errno));
          return -1;
      }
      memcpy(&ipaddr.sin_addr.s_addr, hp->h_addr, hp->h_length);
      ipaddr.sin_family = AF_INET;
#if !defined(__linux__)
      // On BSD, the length is defined in the datastructure
      ipaddr.sin_len = sizeof(struct sockaddr_in);
#endif // __linux__
      ipaddr.sin_port = htons(port);
      err = connect(*client_socket, (struct sockaddr*)&ipaddr, sizeof(ipaddr));
      if (err == -1)
      {
        fprintf(stderr,"### Unable to connect to port %d: %s\n",
                port,strerror(errno));
        return -1;
      }
      printf("Connected  to %s port %d on socket %d\n",address,port,
             *client_socket);
  }
  return 0;
}

static int open_server_socket(char     *address,
                              int       port,
                              int      *client_socket,
                              int      *server_socket)
{
  int    err;
  int    family;
  int    opt[1];

  if (port == 0)
      family = AF_UNIX;
  else
      family = AF_INET;

  *server_socket = socket(family, SOCK_STREAM, 0);
  if (*server_socket == -1)
  {
    fprintf(stderr,"### Unable to create socket: %s\n",strerror(errno));
    return -1;
  }

  // Try to allow address reuse as soon as possible after we've finished
  // with it
  opt[0] = 1;
  (void) setsockopt(*server_socket, SOL_SOCKET, SO_REUSEADDR, opt, sizeof(int));

  if (family == AF_UNIX)
  {
      struct sockaddr_un  unaddr = {0};
      socklen_t           length;
      unaddr.sun_family = AF_UNIX;
      if (strlen(address) > 104)                // apparently 104 is the limit
          printf("!!! Address '%s' is too long, truncated\n",address);
      strncpy(unaddr.sun_path, address, 104);   // yes, really, 104
      unaddr.sun_path[104] = 0;                 // that number again
      // If the file with that name already exists, then we will fail.
      // But I'd rather not delete the file if we've not created it
      // - that would be something of a Bad Thing to do to someone!
      length = sizeof(unaddr.sun_family) + strlen(unaddr.sun_path);

      err = bind(*server_socket, (struct sockaddr *)&unaddr, length);
      if (err == -1)
      {
          fprintf(stderr,"### Unable to bind to %s: %s\n",
                  address,strerror(errno));
          close(*server_socket);
          *server_socket = -1;
          return -1;
      }
  }
  else
  {
      struct sockaddr_in  ipaddr;
#if !defined(__linux__)
      // On BSD, the length is defined in the datastructure
      ipaddr.sin_len = sizeof(struct sockaddr_in);
#endif
      ipaddr.sin_family = AF_INET;
      ipaddr.sin_port = htons(port);        // this port
      ipaddr.sin_addr.s_addr = INADDR_ANY;  // any interface

      err = bind(*server_socket, (struct sockaddr*)&ipaddr, sizeof(ipaddr));
      if (err == -1)
      {
          fprintf(stderr,"### Unable to bind to port %d: %s\n",
                  port,strerror(errno));
          close(*server_socket);
          *server_socket = -1;
          return -1;
      }
  }

  printf("Listening for a connection\n");

  // Listen for someone to connect to it, just one someone
  err = listen(*server_socket,1);
  if (err == -1)
  {
      fprintf(stderr,"### Error listening for client: %s\n",strerror(errno));
      close(*server_socket);
      *server_socket = -1;
      if (family == AF_UNIX)
          (void) unlink(address);
      return -1;
  }

  // Accept the connection
  *client_socket = accept(*server_socket,NULL,NULL);
  if (*client_socket == -1)
  {
      fprintf(stderr,"### Error accepting connection: %s\n",strerror(errno));
      close(*server_socket);
      *server_socket = -1;
      if (family == AF_UNIX)
          (void) unlink(address);
      return -1;
  }

  printf("Connected  via port %d on socket %d\n",port,*client_socket);
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

static int kbus_limpet(uint32_t  kbus_device,
                       char     *message_name,
                       bool      is_server,
                       char     *address,
                       int       port,
                       uint32_t  network_id,
                       char     *termination_message,
                       bool      verbose)
{
    int             rv = 0;
    int             limpet_socket = -1;
    int             listen_socket = -1;
    kbus_ksock_t    ksock = -1;
    uint32_t        other_network_id;
    struct pollfd   fds[2];

    limpet_context_t    context;

    if (network_id < 1) {
        printf("### Limpet network id must be > 0, not %d\n",network_id);
        return -1;
    }

    ksock = kbus_ksock_open(kbus_device, O_RDWR);
    if (ksock < 0) {
        printf("### Cannot open KBUS device %u: %s\n",kbus_device,strerror(errno));
        return -1;
    }

    printf("Opened KBUS device %u\n",kbus_device);

    if (is_server)
        rv = open_server_socket(address, port, &limpet_socket, &listen_socket);
    else
        rv = open_client_socket(address, port, &limpet_socket);
    if (rv) {
        printf("### Cannot open socket\n");
        if (rv) goto tidyup;
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

    context.socket = limpet_socket;
    context.ksock = ksock;
    context.network_id = network_id;
    context.other_network_id = other_network_id;
    context.termination_message = termination_message;

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

        if (fds[0].revents & POLLIN) {
            printf("Message from KBUS\n");
            rv = handle_message_from_kbus(&context);
            if (rv < 0) goto tidyup;
        }

        if (fds[1].revents & POLLIN) {
            printf("Message from other Limpet\n");
            rv = handle_message_from_other_limpet(&context);
            if (rv < 0) goto tidyup;
        }
    }

tidyup:
    if (is_server) {
        if (limpet_socket != -1)
            close(limpet_socket);
        if (listen_socket != -1) {
            close(listen_socket);
            if (port == 0)
                (void) unlink(address);
        }
    } else {
        if (limpet_socket != -1) {
            shutdown(limpet_socket, SHUT_RDWR);
            close(limpet_socket);
            if (port == 0)
                (void) unlink(address);
        }
    }
    if (ksock != -1)
        (void) kbus_ksock_close(ksock);
    return rv;
}

static int int_value(char *cmd,
                     char *arg,
                     int   positive,
                     int   base,
                     long *value)
{
  char  *ptr;
  long   val;
  errno = 0;
  val = strtol(arg,&ptr,base);
  if (errno)
  {
    if (errno == ERANGE && val == 0)
      printf("### String cannot be converted to (long) integer in %s %s\n",
                 cmd,arg);
    else if (errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
      printf("### Number is too big (overflows) in %s %s\n",cmd,arg);
    else
      printf("### Cannot read number in %s %s (%s)\n",
                 cmd,arg,strerror(errno));
    return -1;
  }
  if (ptr[0] != '\0')
  {
    if (ptr-arg == 0)
      printf("### Argument to %s should be a number, in %s %s\n",
                 cmd,cmd,arg);
    else
      printf("### Unexpected characters ('%s') after the %.*s in %s %s\n",
                 ptr,
                 (int)(ptr-arg),arg,
                 cmd,arg);
    return -1;
  }

  if (val > INT_MAX || val < INT_MIN)
  {
    printf("### Value %ld (in %s %s) is too large (to fit into 'int')\n",
               val,cmd,arg);
    return -1;
  }

  if (positive && val < 0)
  {
    printf("### Value %ld (in %s %s) is less than zero\n",
               val,cmd,arg);
    return -1;
  }

  *value = val;
  return 0;
}

static int parse_address(char  *arg,
                         char **address,
                         int   *port)
{
    char *ptr;
    char *colon_ptr = strchr(arg,':');

    *address = arg;

    if (colon_ptr == NULL) {
        // There is no port number
        *port = 0;
        return 0;
    }

    errno = 0;
    *port = strtol(colon_ptr+1,&ptr,10);
    if (errno)
    {
        printf("### Cannot read port number in %s (%s)\n",
               arg,strerror(errno));
        return -1;
    }
    if (ptr[0] != '\0')
    {
        printf("### Unexpected characters in port number in %s\n",arg);
        return -1;
    }
    if (*port < 0)
    {
        printf("### Negative port number in %s\n",arg);
        return -1;
    }
    colon_ptr[0] = '\0';  // yep, modifying arg
    return 0;
}

static void print_usage(void)
{
    printf(
        "Usage: runlimpet <things>\n"
        "\n"
        "This runs a client or server limpet, talking to a server or client limpet\n"
        "(respectively).\n"
        "\n"
        "The <things> specify what the Limpet is to do. The order of <things> on the\n"
        "command line is not significant, but if a later <thing> contradicts an earlier\n"
        "<thing>, the later <thing> wins.\n"
        "\n"
        "<thing> may be:\n"
        "\n"
        "    <host>:<port>   Communicate via the specified host and port.\n"
        "    <path>          Communicate via the named Unix domain socket.\n"
        "\n"
        "        One or the other communication mechanism must be specified.\n"
        "\n"
        "    -s, -server     This is a server Limpet.\n"
        "    -c, -client     This is a client Limpet.\n"
        "\n"
        "        Either client or server must be specified.\n"
        "\n"
        "    -id <number>    Messages sent by this Limpet (to the other Limpet) will\n"
        "                    have network ID <number>. This defaults to 1 for a client\n"
        "                    and 2 for a server. Regardless, it must be greater than\n"
        "                    zero.\n"
        "\n"
        "    -k <number>, -kbus <number>\n"
        "                    Connect to the given KBUS device. The default is to connect\n"
        "                    to KBUS 0.\n"
        "\n"
        "    -m <name>, -message <name>\n"
        "                    Proxy any messages with this name to the other Limpet.\n"
        "                    Using \"-m '$.*'\" will proxy all messages, and this is\n"
        "                    the default.\n"
        );
}

int main(int argc, char **argv)
{
    int          err;
    int          port = 0;               // meaning "no port"
    char        *address = NULL;
    bool         had_address = false;
    int          is_server;
    bool         had_server_or_client = false;
    int          kbus_device = 0;
    int          network_id = -1;       // unset
    char        *message_name = "$.*";
    bool         verbose = false;
    int          ii = 1;

    char        *termination_message = NULL;

    if (argc < 2)
    {
        print_usage();
        return 0;
    }

    while (ii < argc)
    {
        if (argv[ii][0] == '-')
        {
            if (!strcmp("--help",argv[ii]) || !strcmp("-help",argv[ii]) ||
                !strcmp("-h",argv[ii]))
            {
                print_usage();
                return 0;
            }
            else if (!strcmp("-s",argv[ii]) || !strcmp("-server",argv[ii]))
            {
                is_server = true;
                had_server_or_client = true;
            }
            else if (!strcmp("-c",argv[ii]) || !strcmp("-client",argv[ii]))
            {
                is_server = false;
                had_server_or_client = true;
            }
            else if (!strcmp("-id",argv[ii]))
            {
                long  val;
                if (ii+1 == argc)
                {
                    fprintf(stderr,"### %s requires an integer argument (network id)\n",argv[ii]);
                    return 1;
                }
                if (int_value(argv[ii], argv[ii+1], true, 10, &val))
                    return 1;
                network_id = val;
                ii++;
            }
            else if (!strcmp("-k",argv[ii]) || !strcmp("-kbus",argv[ii]))
            {
                long  val;
                if (ii+1 == argc)
                {
                    fprintf(stderr,"### %s requires an integer argument (KBUS device)\n",argv[ii]);
                    return 1;
                }
                if (int_value(argv[ii], argv[ii+1], true, 10, &val))
                    return 1;
                kbus_device = val;
                ii++;
            }
            else if (!strcmp("-m",argv[ii]) || !strcmp("-message",argv[ii]))
            {
                if (ii+1 == argc)
                {
                    fprintf(stderr,"### %s requires an argument (message name)\n",argv[ii]);
                    return 1;
                }
                message_name = argv[ii+1];
                ii++;
            }
            else if (!strcmp("-verbose",argv[ii]) || !strcmp("-v",argv[ii]))
            {
                verbose = true;
            }
            else
            {
                printf("### Unrecognised command line switch '%s'\n",argv[ii]);
                return 1;
            }
        }
        else
        {
            int err = parse_address(argv[ii],&address,&port);
            if (err) return 1;
            had_address = true;
        }
        ii++;
    }

    if (!had_server_or_client) {
        printf("### One of -server or -client must be specified\n");
        return 1;
    }

    if (!had_address) {
        printf("### An address to connect to is required\n");
        return 1;
    }

    if (network_id == -1) {
        network_id = is_server ? 2 : 1;
    }

    printf("Limpet: %s via %s '%s'",
           is_server?"Server":"Client",
           port==0?"Unix domain socket":"TCP/IP, address",
           address);
    if (port != 0)
        printf(" port %d",port);
    printf(" for KBUS %d, using network id %d, listening for '%s'\n",
           kbus_device, network_id, message_name);

    err = kbus_limpet(kbus_device, message_name, is_server, address, port,
                      network_id, termination_message, verbose);
    if (err) return 1;

    return 0;
}

// Local Variables:
// tab-width: 8
// indent-tabs-mode: nil
// c-basic-offset: 4
// End:
// vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
