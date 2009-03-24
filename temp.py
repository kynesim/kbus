import os
from kbus import Message, Interface

f = Interface(0,'rw')

f.bind('$.Fred.*')

m = Message('$.Fred.Jim')
f.write(m)
r = f.read()
assert r.equivalent(m)

m = Message('$.Fred.Bob.William')
f.write(m)
r = f.read()
assert r.equivalent(m)

f.close()
