/* kmsg.c */
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

/* A program you can use to listen for or send kbus messages 
 */

#include <libkbus/kbus.h>
#include <stdio.h>

static void usage(void);


static int create_kbus_message(kbus_message_t **out_hdr,
			       const char *msg_name, const char *fmt, 
			       const char *data, int expect_reply);

static int do_reply(const char *msg_name, int bus_number);
static int do_listen(const char *msg_name, int bus_number);
static int do_send(const char *msg_name, const char *fmt, 
		   const char *data, int expect_reply, int bus_number);

static const char *bus_device_name(int bus_number);

int main(int argn, char *args[])
{
  int bus_number = 0;
  int verbose = 0;

  if (argn < 2)
    {
      usage();
      return 1;
    }
  
  if (!strcmp(args[1], "-bus") || !strcmp(args[1], "--bus"))
    {
      if (argn < 3)
	{
	  fprintf(stderr, "kmsg -bus must have an argument.\r\n");
	  usage();
	}
      bus_number = atoi(args[2]);
      args += 2; argn -= 2;

    }

  const char *cmd= args[1];


  if (!strcmp(cmd, "listen"))
    {
      if (argn != 3)
	{
	  fprintf(stderr, " Wrong number of arguments for listen.\n");
	  usage();
	  return 2;
	}
      return do_listen(args[2], bus_number);
    }
  else if (!strcmp(cmd, "reply"))
    {
      if (argn != 3)
	{
	  fprintf(stderr, " Wrong number of arguments for reply.\n");
	  usage();
	  return 2;
	}
      return do_reply(args[2], bus_number);
    }
  else if (!strcmp(cmd, "send") || 
	   !strcmp(cmd, "call"))
    {
      if (argn != 5)
	{
	  fprintf(stderr, " Wrong number of arguments for send/call.\n");
	  usage();
	  return 3;
	}
      // We're expecting a reply iff the command is not 'send'
      return do_send(args[2], args[3], args[4], 
		     strcmp(cmd, "send"), bus_number);
    }
  else
    {
      fprintf(stderr, "Bad command '%s' \n", cmd);
      usage();
      return 4;
    }
}

static void usage(void)
{
  fprintf(stderr, "Syntax: kmsg [-bus <NN>] [-v] listen|reply|send|call <name> [<fmt> <data>]\n"
	  "\n"
	  "    kmsg listen <name>  - Bind as Listener for <name>, print every message you receive.\n"
	  "    kmsg reply  <name>  - Bind as Replier  for <name>, print every message you receive,\n"
          "                          and then reply to it.\n"
	  "\n"
	  "    kmsg send   <name> <fmt> <data> - Send the given message.\n"
	  "    kmsg call   <name> <fmt> <data> - Send the given message, wait for a reply.\n"
	  "\n"
	  "<fmt> can be 's'tring or 'h'ex.\n"
          "\n"
          "-bus <NN> may be used to choose the KBUS device. The default is 0.\n"
	  "\n");
}

static int do_listen(const char *msg_name, int bus_number)
{
  kbus_ksock_t the_socket;
  int rv;


  the_socket = kbus_ksock_open_by_name(bus_device_name(bus_number), O_RDONLY);
  if (the_socket < 0)
    {
      fprintf(stderr, "Cannot open /dev/kbus0 - %s [%d] \n", 
	      strerror(errno), errno);
      return 10;
    }

  printf("Binding as Listener to '%s'\n", msg_name);
  rv = kbus_ksock_bind(the_socket, msg_name, 0);
  if (rv < 0)
    {
      fprintf(stderr, "Cannot bind() to %s - %s [%d] \n",
	      msg_name, strerror(errno), errno);
      return 11;
    }
  uint32_t  ksock_id;
  (void) kbus_ksock_id(the_socket, &ksock_id);
  printf("..bound on ksock id %u\n",ksock_id);

  while (1)
    {
      kbus_message_t *msg = NULL;

      rv = kbus_wait_for_message(the_socket, KBUS_KSOCK_READABLE);
      if (rv < 0)
	{
	  fprintf(stderr, "Failed to wait for message - %s [%d] \n",
		  strerror(errno), errno);
	  return 3;
	}
		  
      rv = kbus_ksock_read_next_msg(the_socket, &msg);
      if (rv < 0)
	{
	  fprintf(stderr, "Failed to read next message - %s [%d] \n",
		  strerror(errno), errno);
	  return 2;
	}
      
      kbus_msg_print(stdout, msg); fprintf(stdout,"\n");
      //kbus_msg_dump(msg, 1);
      kbus_msg_delete(&msg);
    }

  return 0;
}

static int do_reply(const char *msg_name, int bus_number)
{
  kbus_ksock_t the_socket;
  int rv;


  the_socket = kbus_ksock_open_by_name(bus_device_name(bus_number), O_RDWR);
  if (the_socket < 0)
    {
      fprintf(stderr, "Cannot open /dev/kbus0 - %s [%d] \n", 
	      strerror(errno), errno);
      return 10;
    }

  printf("Binding as Replier to '%s'\n", msg_name);
  rv = kbus_ksock_bind(the_socket, msg_name, 1);
  if (rv < 0)
    {
      fprintf(stderr, "Cannot bind() to %s - %s [%d] \n",
	      msg_name, strerror(errno), errno);
      return 11;
    }

  uint32_t  ksock_id;
  (void) kbus_ksock_id(the_socket, &ksock_id);
  printf("..bound on ksock id %u\n",ksock_id);

  while (1)
    {
      kbus_message_t *msg = NULL;
      kbus_message_t *reply = NULL;
      kbus_msg_id_t   msg_id;

      rv = kbus_wait_for_message(the_socket, KBUS_KSOCK_READABLE);
      if (rv < 0)
	{
	  fprintf(stderr, "Failed to wait for message - %s [%d] \n",
		  strerror(errno), errno);
	  return 3;
	}
		  
      rv = kbus_ksock_read_next_msg(the_socket, &msg);
      if (rv < 0)
	{
	  fprintf(stderr, "Failed to read next message - %s [%d] \n",
		  strerror(errno), errno);
	  return 2;
	}
      
      kbus_msg_print(stdout, msg); fprintf(stdout,"\n");
      //kbus_msg_dump(msg, 1);

      rv = kbus_msg_create_reply_to(&reply, msg, NULL, 0, 0);
      if (rv < 0)
	{
	  fprintf(stderr, "Failed to create reply - %s [%d] \n",
		  strerror(errno), errno);
          kbus_msg_delete(&msg);
	  return rv;
	}

      rv = kbus_ksock_send_msg(the_socket, reply, &msg_id);
      if (rv < 0)
	{
	  fprintf(stderr, "Failed to send reply - %s [%d] \n",
		  strerror(errno), errno);
          kbus_msg_delete(&msg);
          kbus_msg_delete(&reply);
	  return rv;
	}

      reply->id = msg_id;
      fprintf(stdout,"Sent "); kbus_msg_print(stdout, reply); fprintf(stdout,"\n");

      kbus_msg_delete(&msg);
      kbus_msg_delete(&reply);
    }

  return 0;
}

static int hex_to_value(char c)
{
  if (c >= '0' && c <= '9')
    {
      return c-'0';
    }
  else if (c >= 'A' && c <= 'F') 
    {
      return c-'A' + 10;
    }
  else if (c >= 'a' && c <= 'f')
    {
      return c - 'a'+ 10;
    }
  else
    {
      return -1;
    }
}

static int create_kbus_message(kbus_message_t **out_hdr,
			       const char *msg_name, const char *fmt, 
			       const char *data, int expect_reply)
{
  uint8_t *msg_data = NULL;
  uint32_t data_len = 0;

  if (fmt[0] == 's')
    {
      data_len = strlen(data);
      msg_data = (uint8_t *)malloc(strlen(data));
      memcpy(msg_data, data, data_len);
    }
  else if (fmt[0] == 'h')
    {
      int i;
      int in_len;

      /* Hex. This is rather harder .. */
      
      in_len = strlen(data);
      data_len = (in_len>>1)& ~1;
      msg_data = (uint8_t *)malloc(data_len);
      memset(msg_data, '\0', data_len);

      for (i = 0 ; i < in_len; ++i)
	{
	  int v;
	  
	  v = hex_to_value(data[i]);
	  if (v < 0)
	    {
	      fprintf(stderr, " '%c' is not a valid hex digit. \n", data[i]);
	      return 20;
	    }
	  
	  printf("i = %d -> [%d, %d] v = %d \n", 
		 i, (i>>1), i&1, v);
	  msg_data[(i>>1)] |= (i&1) ? (v) : (v<<4);
	}
    }
  
  {
    int i;

    printf("Msg data:\n");
    for (i =0 ;i < data_len; ++i)
      {
	printf("%02x ", msg_data[i]);
      }
    printf("\n");
  }

  return kbus_msg_create(out_hdr, 
			 msg_name, strlen(msg_name),
			 msg_data, data_len, 
			 (expect_reply ? KBUS_BIT_WANT_A_REPLY : 0));
}

static int do_send(const char *msg_name, const char *fmt, 
		   const char *data, int expect_reply, int bus_number)
{
  int rv;
  kbus_message_t *kmsg;

  rv = create_kbus_message(&kmsg, msg_name, fmt, data, expect_reply);
  if (rv)
    {
      fprintf(stderr, "Couldn't create KBus message.\n");
      return rv;
    }

  /* Otherwise .. */
  int ks;

  ks = kbus_ksock_open_by_name(bus_device_name(bus_number), O_RDWR);
  if (ks < 0)
    {
      fprintf(stderr, "Cannot open /dev/kbus0 - %s [%d]\n", 
	      strerror(errno), errno);
      return 20;
    }
  
  struct kbus_msg_id id;

  printf("> Sending %s [want_reply? %d]\n", 
	 msg_name, expect_reply);


  kbus_msg_print(stdout, kmsg); fprintf(stdout,"\n");
  //kbus_msg_dump(kmsg, 1);
  rv = kbus_ksock_send_msg(ks, kmsg, &id);
  if (rv < 0)
    {
      fprintf(stderr, "Cannot send message - %s [%d] \n",
	      strerror(errno), errno);
      return 21;
    }

  printf("> Sent message %d:%d .. \n", id.network_id, id.serial_num);

  if (expect_reply)
    {
      while (1)
	{
	  kbus_message_t *inmsg = NULL;
	  

	  rv = kbus_wait_for_message(ks, KBUS_KSOCK_READABLE);
	  if (rv < 0)
	    {
	      fprintf(stderr, "Failed to wait for message - %s [%d] \n",
		      strerror(errno), errno);
	      return 3;
	    }

          fprintf(stderr, ">Reading message\n");

	  rv = kbus_ksock_read_next_msg(ks, &inmsg);
	  if (rv < 0)
	    {
	      fprintf(stderr, "Cannot read next message: %s [%d] \n", 
		      strerror(errno), errno);
	      return 20;
	    }
	  
          kbus_msg_print(stdout, inmsg); fprintf(stdout,"\n");
	  //kbus_msg_dump(inmsg, 1);
	  
	  if (!kbus_msg_compare_ids(&(inmsg->in_reply_to), &id))
	    {
	      fprintf(stderr, "> Got Reply!\n");
	      break;
	    }

	  kbus_msg_delete(&inmsg);
	}
    }
	   
  // No need to kill everything - we're about exit .. 
   
  return 0;
}

static const char *bus_device_name(int n)
{
  static char buf[64];
  sprintf(buf, "/dev/kbus%d", n);
  return buf;
}


/* End file */



