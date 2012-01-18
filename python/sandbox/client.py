#! /usr/bin/env python

"""KBUS userspace client.

Initial experimentation.
"""

import socket
import sys

from kbus import Message

from server import read_message

def run_client(address):
  """Run our client.
  """

  sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  sock.connect(address)

  while True:
    txt = raw_input('What to send (Q to stop server): ')
    if not txt:
      break

    if txt == 'Q':
      sock.sendall('QUIT')
      break
    elif txt == 'B':      # send a "B"roken message
      sock.sendall('This is not a valid message')
    else:
      msg = Message('$.Test.Message', txt)

    data = msg.to_string()
    sock.sendall(data)

    response = read_message(sock)
    print 'Response: Message %s %s from %s = %s'%(response.id, response.name,
                                                  response.from_, response.data)

  sock.shutdown(socket.SHUT_RDWR)
  sock.close()
  

if __name__ == "__main__":
  if len(sys.argv) != 2:
    print 'Usage: client.py <server address>'
    sys.exit()

  run_client(sys.argv[1])


# vim: set tabstop=8 shiftwidth=2 expandtab:
