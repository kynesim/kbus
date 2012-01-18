/*
 * An example application to run a KBUS Limpet.
 *
 * This is a simple example communicating between two Limpets over a socket.
 * It is not intended to be suitable for production use.
 *
 * It is compatible with the Python version of the same name.
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
 *   Tibs <tibs@tonyibbs.co.uk>
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
#include "libkbus/limpet.h"

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

static int send_message_to_other_limpet(int                limpet_socket,
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

    // And, since we're going to throw it onto the network...
    kbus_serialise_message_header(msg, array);

    rv = send(limpet_socket, array, sizeof(array), 0);
    if (rv < 0) {
        printf("### Error sending message header to other limpet: %s\n",
               strerror(errno));
        return -1;
    }

    padded_name_len = KBUS_PADDED_NAME_LEN(msg->name_len);
    rv = send(limpet_socket, name, msg->name_len, 0);
    if (rv < 0) {
        printf("### Error sending message name to other limpet: %s\n",
               strerror(errno));
        return -1;
    }
    if (padded_name_len - msg->name_len > 0) {
        rv = send(limpet_socket, padding, padded_name_len-msg->name_len, 0);
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
            kbus_limpet_ReplierBindEvent_hton(msg);
        }

        padded_data_len = KBUS_PADDED_DATA_LEN(msg->data_len);
        rv = send(limpet_socket, data, msg->data_len, 0);
        if (rv < 0) {
            printf("### Error sending message data to other limpet: %s\n",
                   strerror(errno));
            return -1;
        }
        if (padded_data_len - msg->data_len > 0) {
            rv = send(limpet_socket, padding, padded_data_len-msg->data_len, 0);
            if (rv < 0) {
                printf("### Error sending message data padding to other limpet: %s\n",
                       strerror(errno));
                return -1;
            }
        }
    }

    // And a final end guard for safety
    rv = send(limpet_socket, &array[KBUS_SERIALISED_HDR_LEN-1], sizeof(uint32_t), 0);
    if (rv < 0) {
        printf("### Error sending final message end guard to other limpet: %s\n",
               strerror(errno));
        return -1;
    }

    return 0;
}

static int read_message_from_other_limpet(int                 limpet_socket,
                                          kbus_message_t    **msg)
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

    length = recv(limpet_socket, array, wanted, MSG_WAITALL);
    if (length == 0) {
        printf("### Trying to read message: other Limpet has gone away\n");
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

    kbus_unserialise_message_header(array, new_msg);

    if (new_msg->start_guard != KBUS_MSG_START_GUARD) {
        printf("### Message start guard from other limpet is %08x, not %08x\n",
               new_msg->start_guard, KBUS_MSG_START_GUARD);
        goto error_return;
    } else if (new_msg->end_guard != KBUS_MSG_END_GUARD) {
        printf("### Message end guard from other limpet is %08x, not %08x\n",
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
    length = recv(limpet_socket, name, padded_name_len, MSG_WAITALL);
    if (length == 0) {
        printf("### Trying to read message name: other Limpet has gone away\n");
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
        length = recv(limpet_socket, data, padded_data_len, MSG_WAITALL);
        if (length == 0) {
            printf("### Trying to read message data: other Limpet has gone away\n");
            goto error_return;
        } else if (length != padded_data_len) {
            printf("### Unable to read whole message data from other Limpet: %s\n",
                   strerror(errno));
            goto error_return;
        }

        new_msg->data = data;

        // We know the structure of Replier Bind Event data, and can mangle
        // it appropriately for having come from the network
        if (!strncmp(name, KBUS_MSG_NAME_REPLIER_BIND_EVENT, new_msg->name_len)) {
            kbus_limpet_ReplierBindEvent_ntoh(new_msg);
        }
    }

    // And read a final end guard
    wanted = sizeof(uint32_t);              // erm, 4...
    length = recv(limpet_socket, &final_end_guard, wanted, MSG_WAITALL);
    if (length == 0) {
        printf("### Trying to read message end guard: other Limpet has gone away\n");
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
static int kbus_limpet(kbus_ksock_t     ksock,
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

    kbus_limpet_context_t    *context;

    kbus_message_t  *msg = NULL;
    kbus_message_t  *error = NULL;

    if (network_id < 1) {
        printf("### Limpet network id must be > 0, not %d\n",network_id);
        return -1;
    }

    if (message_name == NULL) {
        if (verbosity > 1)
            printf("%u Limpet defaulting to proxy messages matching '$.*'\n", network_id);
        message_name = "$.*";
    }

    if (verbosity > 1)
        printf("%u Sending our network id, %u\n", network_id, network_id);
    rv = send_network_id(limpet_socket, network_id);
    if (rv) goto tidyup;

    if (verbosity > 1)
        printf("%u Reading the other limpet's network id\n", network_id);
    rv = read_network_id(limpet_socket, &other_network_id);
    if (rv) goto tidyup;

    if (verbosity)
        printf("%u The other limpet's network id is %u\n", network_id, other_network_id);

    if (other_network_id == network_id) {
        printf("### This Limpet and its pair both have network id %u\n",
               network_id);
        rv = -1;
        goto tidyup;
    }

    rv = kbus_limpet_new_context(ksock, network_id, other_network_id,
                                 message_name, verbosity,
                                 &context);
    if (rv) goto tidyup;

    fds[0].fd = ksock;
    fds[0].events = POLLIN; // We want to read a KBUS message
    fds[1].fd = limpet_socket;
    fds[1].events = POLLIN; // We want to read a message from our pair
    for (;;) {
        int   results;
        char *name;

        fds[0].revents = 0;
        fds[1].revents = 0;
        results = poll(fds, 2, -1);     // No timeout, we're patient
        if (results < 0) {
            printf("### Waiting for messages abandoned: %s\n",strerror(errno));
            goto tidyup;
        }

        if (verbosity > 1)
            printf("\n");

        if (fds[0].revents & POLLIN) {
            bool  terminate;
            if (verbosity > 1)
                printf("%u ----------------- Message from KBUS\n", network_id);
            rv = kbus_ksock_read_next_msg(ksock, &msg);
            if (rv < 0) goto tidyup;

            if (verbosity > 1) {
                printf("%u ----------------- ", network_id);
                kbus_msg_print(stdout, msg);
                printf("\n");
            }

            if (termination_message != NULL) {
                name = kbus_msg_name_ptr(msg);
                if (!strncmp(termination_message, name, msg->name_len)) {
                    if (verbosity > 1) {
                        printf("%u ----------------- Terminated by message %s\n",
                               network_id, termination_message);
                        rv = 0;
                        goto tidyup;
                    }
                }
            }

            rv = kbus_limpet_amend_msg_from_kbus(context, msg);
            if (rv == 0) {
               rv = send_message_to_other_limpet(limpet_socket, msg);
               if (rv) goto tidyup;
            }
            else if (rv < 0) {
                goto tidyup;
            }
        }
        kbus_msg_delete(&msg);

        if (fds[1].revents & POLLIN) {
            if (verbosity > 1)
                printf("%u ----------------- Message from other Limpet\n", network_id);

            rv = read_message_from_other_limpet(limpet_socket, &msg);
            if (rv < 0) goto tidyup;

            rv = kbus_limpet_amend_msg_to_kbus(context, msg, &error);
            if (rv == 0) {
                kbus_msg_id_t    msg_id;
                if (verbosity > 1) {
                    printf("%u ----------------- ", network_id);
                    kbus_msg_print(stdout, msg);
                    printf("\n");
                }
                rv = kbus_ksock_send_msg(ksock, msg, &msg_id);
                if (rv) {
                    rv = kbus_limpet_could_not_send_to_kbus_msg(context, msg,
                                                                rv, &error);
                    if (rv == 0) {
                        rv = send_message_to_other_limpet(limpet_socket, error);
                        if (rv) goto tidyup;
                    } else if (rv < 0) {
                        goto tidyup;
                    }
                }
            } else if (rv == 2) {
                if (verbosity > 1) {
                    printf("%u ----------------- ", network_id);
                    kbus_msg_print(stdout, msg);
                    printf("\n");
                    printf("%u >>>>>>>>>>>>>>>>> ", network_id);
                    kbus_msg_print(stdout, error);
                    printf("\n");
                }
                // an error occurred, tell the other limpet
                rv = send_message_to_other_limpet(limpet_socket, error);
                if (rv) goto tidyup;
            } else if (rv < 0) {
                goto tidyup;
            }
        }
        kbus_msg_delete(&msg);
        kbus_msg_delete(&error);
    }

tidyup:
    kbus_msg_delete(&msg);
    kbus_msg_delete(&error);
    kbus_limpet_free_context(&context);
    return rv;
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

static int run_limpet(uint32_t  kbus_device,
                      char     *message_name,
                      bool      is_server,
                      char     *address,
                      int       port,
                      uint32_t  network_id,
                      char     *termination_message,
                      int       verbosity)
{
    int             rv = 0;
    int             limpet_socket = -1;
    int             listen_socket = -1;
    kbus_ksock_t    ksock = -1;
    uint32_t        other_network_id;
    uint32_t        ksock_id;
    struct pollfd   fds[2];

    ksock = kbus_ksock_open(kbus_device, O_RDWR);
    if (ksock < 0) {
        printf("### Cannot open KBUS device %u: %s\n",kbus_device,strerror(errno));
        return -1;
    }

    printf("Opened KBUS device %u\n",kbus_device);

    if (verbosity > 1)
        (void) kbus_ksock_kernel_module_verbose(ksock, 1);

    if (is_server)
        rv = open_server_socket(address, port, &limpet_socket, &listen_socket);
    else
        rv = open_client_socket(address, port, &limpet_socket);
    if (rv) {
        printf("### Cannot open socket\n");
        if (rv) goto tidyup;
    }

    rv = kbus_limpet(ksock, limpet_socket, network_id, message_name,
                     termination_message, verbosity);

    if (rv) goto tidyup;

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
        "    <host>:<port>   Communicate via the specified host and port\n"
        "                    (the <host> is ignored on the 'server').\n"
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
        "\n"
        "    -v <level>, -verbose <level>\n"
        "                    Change the level of log message output. The default\n"
        "                    is 1. 0 means be quiet, 1 is normal, 2 means output\n"
        "                    information about each message as it is processed.\n"
        "\n"
        "    -t <name>       When the Limpet reads a message named <name> from\n"
        "                    KBUS, it should terminate.\n"
        "\n"
        "This is an example application, not intended to production use.\n"
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
    int          verbosity = 1;
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
            else if (!strcmp("-t",argv[ii]))
            {
                if (ii+1 == argc)
                {
                    fprintf(stderr,"### %s requires an argument (message name)\n",argv[ii]);
                    return 1;
                }
                termination_message = argv[ii+1];
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
            else if (!strcmp("-v",argv[ii]) || !strcmp("-verbose",argv[ii]))
            {
                long  val;
                if (ii+1 == argc)
                {
                    fprintf(stderr,"### %s requires an integer argument (verbosity level)\n",argv[ii]);
                    return 1;
                }
                if (int_value(argv[ii], argv[ii+1], true, 10, &val))
                    return 1;
                verbosity = val;
                ii++;
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

    printf("C Limpet: %s via %s '%s'",
           is_server?"Server":"Client",
           port==0?"Unix domain socket":"TCP/IP, address",
           address);
    if (port != 0)
        printf(" port %d",port);
    printf(" for KBUS %d, using network id %d, listening for '%s'\n",
           kbus_device, network_id, message_name);

    err = run_limpet(kbus_device, message_name, is_server, address, port,
                     network_id, termination_message, verbosity);
    if (err) return 1;

    return 0;
}

// Local Variables:
// tab-width: 8
// indent-tabs-mode: nil
// c-basic-offset: 4
// End:
// vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
