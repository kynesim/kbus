/*
 * An application to run a KBUS Limpet.
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
#include "libkbus/limpet.h"

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
        "\n"
        "    -v <level>, -verbose <level>\n"
        "                    Change the level of log message output. The default\n"
        "                    is 1. 0 means be quiet, 1 is normal, 2 means output\n"
        "                    information about each message as it is processed.\n"
        "\n"
        "    -t <name>       When the Limpet reads a message named <name> from\n"
        "                    KBUS, it should terminate.\n"
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
