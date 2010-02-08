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

#if 0
static int run_client(char *hostname,
                      int   port)
{
  int output;
  int result;
  struct hostent *hp;
  struct sockaddr_in ipaddr;

  // SOCK_DGRAM => UDP
  output = socket(AF_INET, SOCK_DGRAM, 0);
  if (output == -1)
  {
    fprintf(stderr,"### Unable to create socket: %s\n",strerror(errno));
    return -1;
  }

  hp = gethostbyname(hostname);
  if (hp == NULL)
  {
    fprintf(stderr,"### Unable to resolve host %s: %s\n",
            hostname,strerror(h_errno));
    return -1;
  }
  memcpy(&ipaddr.sin_addr.s_addr, hp->h_addr, hp->h_length);
  ipaddr.sin_family = hp->h_addrtype;
#if !defined(__linux__)
  // On BSD, the length is defined in the datastructure
  ipaddr.sin_len = sizeof(struct sockaddr_in);
#endif // __linux__
  ipaddr.sin_port = htons(port);

  result = connect(output,(struct sockaddr*)&ipaddr,sizeof(ipaddr));
  if (result < 0)
  {
    fprintf(stderr,"### Unable to connect to host %s: %s\n",
            hostname,strerror(errno));
    return -1;
  }
  printf("Connected  to %s on socket %d\n",hostname,output);
  return output;
}

static int run_server(int    port,
                      int    listen_port)
{
  int    err;
  SOCKET server_socket;
  SOCKET client_socket;
  struct sockaddr_in ipaddr;
  byte  *data;

  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1)
  {
    fprintf(stderr,"### Unable to create socket: %s\n",strerror(errno));
    return 1;
  }

  // Bind it to port `listen_port` on this machine
  memset(&ipaddr,0,sizeof(ipaddr));
#if !defined(__linux__)
  // On BSD, the length is defined in the datastructure
  ipaddr.sin_len = sizeof(struct sockaddr_in);
#endif
  ipaddr.sin_family = AF_INET;
  ipaddr.sin_port = htons(listen_port);
  ipaddr.sin_addr.s_addr = INADDR_ANY;  // any interface

  err = bind(server_socket,(struct sockaddr*)&ipaddr,sizeof(ipaddr));
  if (err == -1)
  {
    fprintf(stderr,"### Unable to bind to port %d: %s\n",
            listen_port,strerror(errno));
    return 1;
  }

  for (;;)
  {
    printf("Listening for a connection on port %d\n",listen_port);

    // Listen for someone to connect to it
    err = listen(server_socket,1);
    if (err == -1)
    {
      fprintf(stderr,"### Error listening for client: %s\n",strerror(errno));
      free(data);
      return 1;
    }

    // Accept the connection
    client_socket = accept(server_socket,NULL,NULL);
    if (client_socket == -1)
    {
      fprintf(stderr,"### Error accepting connection: %s\n",strerror(errno));
      free(data);
      return 1;
    }

    // And presumably do something with it...

    close(client_socket);
  }
  return 0;
}
#endif

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
    return 1;
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
    return 1;
  }

  if (val > INT_MAX || val < INT_MIN)
  {
    printf("### Value %ld (in %s %s) is too large (to fit into 'int')\n",
               val,cmd,arg);
    return 1;
  }

  if (positive && val < 0)
  {
    printf("### Value %ld (in %s %s) is less than zero\n",
               val,cmd,arg);
    return 1;
  }

  *value = val;
  return 0;
}

static int host_value(char  *cmd,
                      char  *arg,
                      char **hostname,
                      int   *port)
{
  char *p = strchr(arg,':');

  *hostname = arg;

  if (p != NULL)
  {
    char *ptr;
    p[0] = '\0';  // yep, modifying argv[ii+1]
    errno = 0;
    *port = strtol(p+1,&ptr,10);
    if (errno)
    {
      p[0] = ':';
      printf("### ");
      if (cmd)
        printf("Cannot read port number in %s %s (%s)\n",
               cmd,arg,strerror(errno));
      else
        printf("Cannot read port number in %s (%s)\n",
               arg,strerror(errno));
      return 1;
    }
    if (ptr[0] != '\0')
    {
      p[0] = ':';
      printf("### ");
      if (cmd)
        printf("Unexpected characters in port number in %s %s\n",
               cmd,arg);
      else
        printf("Unexpected characters in port number in %s\n",arg);
      return 1;
    }
    if (*port < 0)
    {
      p[0] = ':';
      printf("### ");
      if (cmd)
        printf("Negative port number in %s %s\n",cmd,arg);
      else
        printf("Negative port number in %s\n",arg);
      return 1;
    }
  }
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
    int          port = 0;               // meaning "no port"
    char        *address = NULL;
    bool         had_address = false;
    int          is_server;
    bool         had_server_or_client = false;
    int          kbus_device = 0;
    int          network_id = -1;       // unset
    char        *message_name = NULL;
    bool         verbose = false;
    int          ii = 1;

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
                ii++;
            }
            else if (!strcmp("-c",argv[ii]) || !strcmp("-client",argv[ii]))
            {
                is_server = false;
                had_server_or_client = true;
                ii++;
            }
            else if (!strcmp("-id",argv[ii]))
            {
                long  val;
                if (ii+1 == argc)
                {
                    fprintf(stderr,"### Missing argument to %s\n",argv[ii]);
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
                    fprintf(stderr,"### Missing argument to %s\n",argv[ii]);
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
                    fprintf(stderr,"### Missing argument to %s\n",argv[ii]);
                    return 1;
                }
                message_name = argv[ii+1];      // NB: not a copy
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
            int err = host_value(argv[ii],argv[ii+1],&address,&port);
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

    printf("Limpet: %s via %s for KBUS %d, using network id %d\n",
           is_server?"Server":"Client", address, kbus_device, network_id);

    return 0;
}

// Local Variables:
// tab-width: 8
// indent-tabs-mode: nil
// c-basic-offset: 4
// End:
// vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
