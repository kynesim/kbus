import os
from kbus import Message, bind, next_len
from array import array

print 'Opening device f1 for read/write'
f1 = open('/dev/kbus0','wb+')
print 'Opening device f2 for read'
f2 = open('/dev/kbus0','rb')

print 'Binding f1 to $.Fred (three times)'
bind(f1,'$.Fred')
bind(f1,'$.Fred',False)
bind(f1,'$.Fred',False)

print 'Binding f2 to $.Jim'
bind(f2,'$.Jim')

print 'Writing to $.Fred on f1 - writes messages N, N+1, N+2'
msg = Message('$.Fred','data')
msg.to_file(f1)

print 'Writing to $.William on f1'
try:
	msg = Message('$.William','data')
	msg.to_file(f1)
except IOError, exc:
	print exc.args
	if exc.args[0] == 99:
		print '...and nobody is listening, which is correct'
	else:
		raise exc

print 'Writing to $.Jim on f1 - writes message N+4'
msg = Message('$.Jim','data')
msg.to_file(f1)

print 'Reading f1 - message N'
data = Message(f1.read(next_len(f1)))
print data.extract()
n0 = data.extract()[0]

print 'Reading f2 - should be message N+3 ...',
data = Message(f2.read(msg.length*4))
n = data.extract()[0]
if n != n0+3:
	print 'but it is not'
else:
	print 'and it is'
print data.extract()

print 'Reading f1 - should be message N+1 ...',
data = Message(f1.read(msg.length*4))
n = data.extract()[0]
if n != n0+1:
	print 'but it is not'
else:
	print 'and it is'
print data.extract()

print 'Reading f1 - should be message N+2 ...',
data = Message(f1.read(msg.length*4))
n = data.extract()[0]
if n != n0+2:
	print 'but it is not'
else:
	print 'and it is'
print data.extract()

data = f1.read(1)
if data:
	print 'Oops - unexpected data on f1'
	print repr(data)
else:
	print 'No more messages on f1'

data = f2.read(1)
if data:
	print 'Oops - unexpected data on f2'
	print repr(data)
else:
	print 'No more messages on f2'


print 'Closing devices'
f1.close()
f2.close()
