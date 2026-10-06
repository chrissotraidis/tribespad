"""Independent quiet binary32 comparison and guest flag/status model."""
import struct
def compare(left,right,control):
 a=left&0x7fffffff;b=right&0x7fffffff
 if a>0x7f800000 or b>0x7f800000:return 0x45,int(a>0x7f800000 and not left&0x400000 or b>0x7f800000 and not right&0x400000)
 den_a=0<a<0x800000;den_b=0<b<0x800000
 if control&0x40:
  if den_a:left&=0x80000000
  if den_b:right&=0x80000000
 av=struct.unpack('<f',struct.pack('<I',left))[0];bv=struct.unpack('<f',struct.pack('<I',right))[0]
 return (0x40 if av==bv else 1 if av<bv else 0),2 if not control&0x40 and (den_a or den_b) else 0
