"""Independent finite float comparison and raw NaN/status selection model."""
import struct
def select(maximum,left,right,control):
 a=left&0x7fffffff;b=right&0x7fffffff;nan=a>0x7f800000 or b>0x7f800000;den_a=0<a<0x800000;den_b=0<b<0x800000
 if control&0x40:
  if den_a:left&=0x80000000
  if den_b:right&=0x80000000
 if nan:return right,1
 av=struct.unpack('<f',struct.pack('<I',left))[0];bv=struct.unpack('<f',struct.pack('<I',right))[0]
 return (left if (av>bv if maximum else av<bv) else right),2 if not control&0x40 and (den_a or den_b) else 0
