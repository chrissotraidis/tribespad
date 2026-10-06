"""Independent legacy binary64 predicate model using exact finite host comparisons."""
import struct
def compare(left,right,predicate,control):
 predicate&=7;a=left&0x7fffffffffffffff;b=right&0x7fffffffffffffff;unordered=a>0x7ff0000000000000 or b>0x7ff0000000000000
 status=0
 if unordered:
  status=int(predicate in [1,2,5,6] or a>0x7ff0000000000000 and not left&0x8000000000000 or b>0x7ff0000000000000 and not right&0x8000000000000)
  eq=lt=False
 else:
  da=0<a<0x10000000000000;db=0<b<0x10000000000000
  if control&64:
   if da:left&=0x8000000000000000
   if db:right&=0x8000000000000000
  elif da or db:status=2
  av=struct.unpack('<d',struct.pack('<Q',left))[0];bv=struct.unpack('<d',struct.pack('<Q',right))[0];eq=av==bv;lt=av<bv
 truth=[eq,lt,lt or eq,unordered,not eq,not lt,not (lt or eq),not unordered][predicate]
 return (0xffffffffffffffff if truth else 0),status
