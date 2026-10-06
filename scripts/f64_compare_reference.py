"""Independent ordered binary64 comparison and guest status/flag model."""
import struct
def compare(left,right,control):
 a=left&0x7fffffffffffffff;b=right&0x7fffffffffffffff
 if a>0x7ff0000000000000 or b>0x7ff0000000000000:return 0x45,1
 den_a=0<a<0x10000000000000;den_b=0<b<0x10000000000000
 if control&0x40:
  if den_a:left&=0x8000000000000000
  if den_b:right&=0x8000000000000000
 av=struct.unpack('<d',struct.pack('<Q',left))[0];bv=struct.unpack('<d',struct.pack('<Q',right))[0]
 return (0x40 if av==bv else 1 if av<bv else 0),2 if not control&0x40 and (den_a or den_b) else 0
