#! /usr/bin/env python

"""KBUS userspace server.

Initial experimentation.
"""

from __future__ import division

import ctypes
import os
import socket
import sys

from kbus import Message
from kbus.messages import _MessageHeaderStruct, _struct_from_string

MSG_HEADER_LEN = ctypes.sizeof(_MessageHeaderStruct)

class ClientGoneAway(Exception):
  """The client has closed its end of the socket.
  """
  pass

class OutOfBoundQuit(Exception):
  """We were asked to Quit
  """
  pass

class BadMessage(Exception):
  """Read a badly formatted KBUS message.
  """
  pass

def padded_name_len(name_len):
  """Calculate the length of a message name, in bytes, after padding.

  Matches the definition in the kernel module's header file
  """
  return 4 * ((name_len + 1 + 3) // 4)

def padded_data_len(data_len):
  """Calculate the length of message data, in bytes, after padding.

  Matches the definition in the kernel module's header file
  """
  return 4 * ((data_len + 3) // 4)

def entire_message_len(name_len, data_len):
  """Calculate the "entire" message length, from the name and data lengths.

  All lengths are in bytes.

  Matches the definition in the kernel module's header file
  """
  return MSG_HEADER_LEN + padded_name_len(name_len) + \
                          padded_data_len(data_len) + 4

def read_message(sock):
  """Read a message from the socket.

  Returns the corresponding Message instance.
  """

  # All KBUS messages start with the start guard:
  print 'Waiting for a message start guard: length 4'
  start_guard = sock.recv(4, socket.MSG_WAITALL)
  if start_guard == '':
    raise ClientGoneAway()
  elif start_guard == 'QUIT': # Sort-of out of bound data
    raise OutOfBoundQuit('Client asked us to QUIT')
  elif start_guard != 'kbus': # This is perhaps a bit naughty, relying on byte order
    raise BadMessage('Data read starts with "%s", not "kbus"'%start_guard)

  # So we start with the message header - this is always the same length
  print 'Read the rest of message header: length',MSG_HEADER_LEN-4
  rest_of_header_data = sock.recv(MSG_HEADER_LEN-4, socket.MSG_WAITALL)

  header_data = start_guard + rest_of_header_data

  header = _struct_from_string(_MessageHeaderStruct, header_data)

  overall_length = entire_message_len(header.name_len, header.data_len)

  print 'Message header: header length %d, total length %d'%(MSG_HEADER_LEN,
      overall_length)

  print 'Reading rest of message: length', overall_length - MSG_HEADER_LEN
  rest_of_message = sock.recv(overall_length - MSG_HEADER_LEN, socket.MSG_WAITALL)

  return Message(header_data + rest_of_message)

def message_generator(connection):
  """Retrieve messages as they arrive.
  """
  while 1:
    try:
      msg = read_message(connection)
      yield msg
    except ClientGoneAway:
      return
    except OutOfBoundQuit:
      yield None

def remove_socket_file(name):
  # Assuming this is an address representing a file in the filesystem,
  # delete it so we can use it again...
  try:
    os.remove(name)
  except Exception as err:
    print 'Unable to delete %s: %s'%(name, err)

def run_server(listen_address):
  """Listen for connections, and deal with them.
  """

  keep_listening = True

  print 'Listening on', listen_address
  sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  # Try to allow address reuse as soon as possible after we've finished
  # with it
  sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
  sock.bind(listen_address)
  try:
    while keep_listening:
      sock.listen(1)
      connection, address = sock.accept()
      print 'Connected to by (%s, %s)'%(connection, address)

      generator = message_generator(connection)
      for msg in generator:
        if msg is None:
          keep_listening = False
          break
        else:
          print 'Received: Message %s %s from %s = %s'%(msg.id, msg.name,
                                                        msg.from_, msg.data)
          connection.sendall(Message('$.Test.Result','OK').to_string())

      print 'Closing connection'
      connection.close()
  except BadMessage as err:
    print err
    connection.sendall(Message('$.Test.Result',str(err)).to_string())
  finally:
    print 'Closing server'
    sock.close()
    remove_socket_file(listen_address)


if __name__ == "__main__":
  if len(sys.argv) != 2:
    print 'Usage: server2.py <server address>'
    sys.exit()

  run_server(sys.argv[1])

# vim: set tabstop=8 shiftwidth=2 expandtab:
