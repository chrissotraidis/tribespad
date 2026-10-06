#!/usr/bin/env python3
"""Original private CRC bulk-copy helper: scalar execution plus finite vector model.

This establishes the seven-stack-argument copy boundary for lengths
at least 256. It is not physical EVEX execution or a runtime interpreter.
"""
import hashlib,json,pathlib,random,struct
import pefile,unicorn
from unicorn import x86_const as X
from qol_checksum_vector_reference import VectorReference
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
image=R/'ref/product/IFC22.dll'
assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
m=pefile.PE(str(image)).get_memory_mapped_image()
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
u.mem_map(0x10000000,(len(m)+4095)&~4095);u.mem_write(0x10000000,m)
u.mem_map(0x200000,0x20000);u.mem_map(0x300000,0x40000);u.mem_map(0x400000,0x1000)
model=VectorReference(u,[(0x100c9f90,0x100ca1ee)])
u.hook_add(unicorn.UC_HOOK_CODE,model.hook)
# Independently compute each polynomial coefficient by parity of diagonals,
# unlike the instruction model's set-bit shifting product.
# Python 3.8 has no int.bit_count.
def product(a,b):
 ar=int('{:064b}'.format(a)[::-1],2)
 return sum((bin(ar & (b << (63-k) if k<=63 else b >> (k-63))).count('1')&1)<<k for k in range(127))
mask=(1<<64)-1
k4=(0x1c6e41596,0x154442bd4);k16=(0x1322d1430,0x11542778a)
assert m[0x149700:0x149740]==struct.pack('<2Q',*k4)*4
assert m[0x1496c0:0x149700]==struct.pack('<2Q',*k16)*4

def fold(value,constants,new):
 return product(value>>64,constants[0]) ^ product(value&mask,constants[1]) ^ new

def expected(state,data,initial,first):
 count=len(data)//256*256
 def lanes(buf):return [int.from_bytes(buf[j:j+16],'little') for j in range(0,64,16)]
 acc=[lanes(data[j:j+64]) for j in range(0,256,64)]
 if first:acc[0][0]^=int.from_bytes(initial,'little')
 for lane in range(4):acc[0][lane]=fold(int.from_bytes(state[lane*16:lane*16+16],'little'),k4,acc[0][lane])
 for offset in range(256,count,256):
  for block in range(4):
   more=lanes(data[offset+block*64:offset+block*64+64])
   acc[block]=[fold(value,k16,new) for value,new in zip(acc[block],more)]
 for block in range(1,4):acc[0]=[fold(value,k4,new) for value,new in zip(acc[0],acc[block])]
 return count,b''.join(value.to_bytes(16,'little') for value in acc[0])

rng=random.Random(0x9f90)
specs=[]
for bit in range(512):specs.append((256,(1<<bit).to_bytes(64,'little'),0,None))
lengths=[256,257,271,383,511,512,513,767,768,769,1023,1024,1025,2048,4095,4096,16384,65536]
for length in lengths:
 for pattern in [0,1,2]:specs.append((length,bytes(rng.randrange(256) for _ in range(64)),pattern,None))
for length in [256,257,512,513,768,1024]:
 for distance in [-257,-256,-255,-128,-1,0,1,15,63,64,127,128,255,256,257]:
  for pattern in [0,1,2]:specs.append((length,bytes(rng.randrange(256) for _ in range(64)),pattern,distance))
cases=[]
for number,(length,state,pattern,distance) in enumerate(specs):
 align=number%16;source=0x1000+align;destination=0x20000+align if distance is None else source+distance
 data=bytes(length) if pattern==0 else (bytes([255])*length if pattern==1 else bytes((j*17+length*3)&255 for j in range(length)))
 arena=bytearray(b'\xa6'*0x31000);offsets=[17,66,115,164]
 arena[source:source+length]=data
 for lane,offset in enumerate(offsets):arena[offset:offset+16]=state[lane*16:lane*16+16]
 u.mem_write(0x300000,bytes(arena));pointers=[0x300000+offset for offset in offsets]
 sp=0x21f003;stack=struct.pack('<8I',0x400000,*pointers,0x300000+destination,0x300000+source,length)
 u.mem_write(sp,stack);u.reg_write(X.UC_X86_REG_ESP,sp);u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
 preserved=[(reg,0x13570000+j*0x10203) for j,reg in enumerate([X.UC_X86_REG_EBX,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI])]
 for reg,value in preserved:u.reg_write(reg,value)
 model.v=[bytes([j+1])*64 for j in range(8)];model.k=[0]*8
 u.emu_start(0x100c9f90,0x400000,count=5000000)
 # Snapshot all256 bytes before each block store, then read the next block
 # from the progressively modified arena. This tests both overlap directions.
 consumed=bytearray()
 for offset in range(0,length//256*256,256):
  chunk=bytes(arena[source+offset:source+offset+256]);consumed.extend(chunk)
  arena[destination+offset:destination+offset+256]=chunk
 count,result=expected(state,bytes(consumed),bytes(16),0)
 assert u.reg_read(X.UC_X86_REG_EIP)==0x400000 and u.reg_read(X.UC_X86_REG_ESP)==sp+4
 assert u.reg_read(X.UC_X86_REG_EAX)==count
 for lane,offset in enumerate(offsets):arena[offset:offset+16]=result[lane*16:lane*16+16]
 assert bytes(u.mem_read(0x300000,len(arena)))==bytes(arena),(number,length,distance)
 assert bytes(u.mem_read(sp,len(stack)))==stack and all(u.reg_read(reg)==value for reg,value in preserved)
 assert all(v[16:]==bytes(48) for v in model.v)
 cases.append(dict(length=length,alignment=align,pattern=pattern,distance=distance,state_hex=state.hex(),result_hex=result.hex(),processed=count))
 if number%100==0:print('Verified original bulk copy',number,flush=True)
assert model.tail_calls==0
out=R/'generated/qol-crc-bulk-copy-reference';out.mkdir(exist_ok=True)
report={'pass':True,'image_sha256':sha(image),'entry':'0x100c9f90','end_exclusive':'0x100ca1ee','body_sha256':hashlib.sha256(m[0xc9f90:0xca1ee]).hexdigest(),'source_sha256':{str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/qol_checksum_vector_reference.py']},'cases':cases,'vector_sites':[dict(pc=hex(pc),**v) for pc,v in sorted(model.sites.items())],'scope':'Seven stack arguments; length>=256; state disjoint from input/output. Source/destination disjoint or overlapping both directions, snapshot256-before-store ordering. Original scalar flow and finite offline vector model with independent coefficient-parity oracle; no substituted callees. Complete arena/arguments/calleeGPRs/processedcount/upperclearing. Not physicalEVEX/nativeclient.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(cases),'original bulk CRC copy cases')
