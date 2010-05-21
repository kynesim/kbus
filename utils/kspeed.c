/* kspeed.c */
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
 *   Richard Watts <rrw@kynesim.co.uk>
 *
 * ***** END LICENSE BLOCK *****
 */

/* A program you can use to check how fast kbus is at sending or receiving
 * messages.
 */

#include <stdio.h>
#include "libkbus/kbus.h"

#define NUMBER_OF_TIMES  1000

static void usage(void)
{
  fprintf(stderr,
          "Syntax: kspeed [-bus <n>] ( send <msgname> <bytes> | listen <msgname> )\n"
          "\n"
          "This program performs a transfer speed test by sending many kbus\n"
          "messages to the specified bus (which defaults to 0) and message\n"
          "name and counting how long it takes to both send and receive.\n"
          "\n"
          "When sending, messages will be given <bytes> bytes of data.\n"
          "\n"
          "You may run as many listeners as you like, but only one sender per\n"
          "message name.\n"
         );
}

static int do_listen(int ksock_fd, const char *kbus_name, const char *msg_name)
{
  int which = 0;
  struct timeval tv_then, tv_now;
  int rv;

  rv = kbus_ksock_bind(ksock_fd, msg_name, 0);
  if (rv < 0)
  {
    fprintf(stderr, "Cannot bind() to kbus %s - %s [%d] \n",
            msg_name, strerror(errno), errno);
    return 2;
  }

  printf("> Listening to %s on %s .. \n", msg_name, kbus_name);
  while (1)
  {
    int i;

    gettimeofday(&tv_then, NULL);
    which = 0;

    for (i =0 ; i < NUMBER_OF_TIMES; ++i)
    {
      kbus_message_t *msg;

      rv = kbus_wait_for_message(ksock_fd, KBUS_KSOCK_READABLE);
      if (rv < 0)
      {
        fprintf(stderr, "Cannot wait for message - %s [%d] \n",
                strerror(errno), errno);
        return 3;
      }

      rv = kbus_ksock_read_next_msg(ksock_fd, &msg);
      if (rv < 0)
      {
        fprintf(stderr, "Cannot read message from kbus - %s [%d] \n",
                strerror(errno), errno);
      }

      if (msg)
      {
        kbus_msg_delete(&msg);
        ++which;
      }
    }

    gettimeofday(&tv_now, NULL);
    {
      double times = NUMBER_OF_TIMES;
      double ms_between = ((double)(tv_now.tv_usec - tv_then.tv_usec) / times) + 
        ((double)(tv_now.tv_sec - tv_then.tv_sec) * times);

      printf("> Recvd %d messages in %g ms => %g msgs/ms \n",
             which, ms_between, (double)which/ ms_between);
    }
  }

  return 0;
}

static int do_send(int ksock_fd, const char *kbus_name, const char *msg_name, int nr_bytes)
{
  int which = 0;
  struct timeval tv_then, tv_now;
  char *data = malloc(nr_bytes);

  memset(data, 0x55, nr_bytes);


  printf("> Sending %d bytes to %s on %s .. \n",
         nr_bytes, msg_name, kbus_name);

  while (1)
  {
    int i;

    which = 0;
    gettimeofday(&tv_then, NULL);

    for (i =0 ;i < NUMBER_OF_TIMES; ++i)
    {
      kbus_message_t *msg;
      struct kbus_msg_id id;
      int rv;

      rv = kbus_msg_create(&msg, 
                           msg_name, strlen(msg_name),
                           data, nr_bytes, 
                           0);
      if (rv < 0)
      {
        fprintf(stderr, "Cannot create kbus message: %s [%d] \n",
                strerror(errno), errno);
        return 4;
      }

      rv = kbus_ksock_send_msg(ksock_fd,
                               msg,
                               &id);
      if (rv < 0)
      {
        if (errno == EINTR || errno == EAGAIN)
        {
          // Who cares?
        }
        else
        {
          fprintf(stderr, "Cannot send kbus message - %s [%d] \n",
                  strerror(errno), errno);
          return 4;
        }
      }
      else
      {
        ++which;
      }

      kbus_msg_delete(&msg);
    }

    gettimeofday(&tv_now, NULL);
    {
      double times = NUMBER_OF_TIMES;
      double ms_between = ((double)(tv_now.tv_usec - tv_then.tv_usec) / times) + 
        ((double)(tv_now.tv_sec - tv_then.tv_sec) * times);

      printf("> Sent %d messages in %g ms => %g msgs/ms \n",
             which, ms_between, (double)which / ms_between);
    }
  }
  return 0;
}

int main(int argn, char *args[])
{
  int bus_number = 0;
  char kname[128];

  if (argn < 2)
  {
    usage();
    return 1;
  }


  if (!strcmp(args[1], "-bus") || !strcmp(args[1], "--bus"))
  {
    if (argn < 3)
    {
      fprintf(stderr, "kspeed %s must have an argument.\n",args[1]);
      usage();
    }
    bus_number = atoi(args[2]);
    args += 2; argn -= 2;
  }



  {
    const char *cmd = args[1];
    const char *msgname = args[2];
    int which  = 0;
    struct timeval tv_then, tv_now;
    int ks, rv;

    sprintf(kname, "/dev/kbus%d", bus_number);

    ks = kbus_ksock_open_by_name(kname, O_RDWR);
    if (ks < 0)
    {
      fprintf(stderr, "Cannot kbus_open() %s - %s [%d] \n",
              kname, strerror(errno), errno);

      return 2;
    }

    if (!strcmp(cmd, "listen"))
    {
      if (argn != 3)
      {
        usage();
        return 1;
      }
      return do_listen(ks, kname, msgname);
    }
    else if (!strcmp(cmd, "send"))
    {
      int nr_bytes;

      if (argn != 4)
      {
        fprintf(stderr, "Not enough arguments\n");
        usage();
        return 1;
      }

      nr_bytes = atoi(args[3]);
      if (nr_bytes < 0)
      {
        fprintf(stderr, "'%s' is not a positive integer. Try again.\n",
                args[3]);
        usage();
        return 1;
      }

      return do_send(ks, kname, msgname, nr_bytes);
    }
    else
    {
      fprintf(stderr, "Invalid command '%s' \n",
              cmd);
      usage();
      return 1;
    }
  }
}
