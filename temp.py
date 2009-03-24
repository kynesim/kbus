import os
from kbus import Message, Interface

f = Interface(0,'rw')

f.bind('$')

f.close()
