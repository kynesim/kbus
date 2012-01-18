/* inspeed.c */

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

/**
 *  How long do inotifies take?
 */

#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <sys/errno.h>
#include <sys/poll.h>


#define NUMBER_OF_TIMES  1000

static void usage(void)
{
  fprintf(stderr,
          "Syntax: inspeed ( send | listen ) <filename>\n"
          "\n"
          "This program tests the speed of inotifies, for crude comparison\n"
          "with the speed of KBUS messages (see kspeed).\n"
          "\n"
          "Sending loops opening and (immediately) closing the given <filename>.\n"
          "Listening loops using inotify to detect that the file has changed.\n"
         );
}

int main(int argn, char *args[])
{
  const char *file_name;
  struct timeval tv_then, tv_now;
  int which;

  if (argn != 3)
  {
    usage();
    return 1;
  }

  file_name = args[2];

  if (!strcmp(args[1], "send"))
  {
    /* Do a send. */
    while (1)
    {
      int i;
      int fd, rv;

      gettimeofday(&tv_then, NULL);
      which = 0;

      for (i =0 ;i < NUMBER_OF_TIMES; ++i)
      {
        fd = open(file_name, O_WRONLY | O_CREAT, 0644);
        if (fd < 0)
        {
          fprintf(stderr, "Cannot open %s: %s (%d)\n", file_name,
                  strerror(errno), errno);
          return 1;
        }
        rv = close(fd);
        if (!rv)
        {
          ++which;
        }
      }

      gettimeofday(&tv_now, NULL);

      {
        double times = NUMBER_OF_TIMES;
        double ms_between = ((double)(tv_now.tv_usec - tv_then.tv_usec) / times) + 
          ((double)(tv_now.tv_sec - tv_then.tv_sec) * times);

        printf("> Sent %d messages in %g ms => %g msgs/m \n",
               which, ms_between, (double)which/ ms_between);
      }
    }
  }
  else if (!strcmp(args[1], "listen"))
  {
    /* Listen .. */
    int fd, rv;

    fd = inotify_init();
    if (fd < 0)
    {
      fprintf(stderr, "Cannot init inotify(): %s (%d)\n",
              strerror(errno), errno);
      return 1;
    }

    rv = inotify_add_watch(fd, file_name, IN_CLOSE_WRITE);
    if (rv < 0)
    {
      fprintf(stderr, "Cannot init inotify_add_watch(): %s (%d)\n",
              strerror(errno), errno);
      return 1;
    }

    while (1)
    {
      int i;

      gettimeofday(&tv_then, NULL);
      which = 0;

      for (i =0 ;i < NUMBER_OF_TIMES; ++i)
      {
        struct pollfd pfds[1];
        int rv;

        pfds[0].revents = 0;
        pfds[0].events = POLLIN ;
        pfds[0].fd = fd;


        rv = poll(&pfds[0], 1, -1);
        if (rv)
        {
          struct inotify_event ine;

          rv = read(fd, &ine, sizeof(struct inotify_event));
          if (rv != sizeof(struct inotify_event))
          {
            fprintf(stderr, "WARNING: Wanted %d bytes, got %d\n",
                    sizeof(struct inotify_event), rv);
          }
          else
          {
            ++which;
          }
        }
      }

      gettimeofday(&tv_now, NULL);

      {
        double times = NUMBER_OF_TIMES;
        double ms_between = ((double)(tv_now.tv_usec - tv_then.tv_usec) / times) + 
          ((double)(tv_now.tv_sec - tv_then.tv_sec) * times);

        printf("> Read %d messages in %g ms => %g msgs/m \n",
               which, ms_between, (double)which/ ms_between);
      }
    }
  }
}
