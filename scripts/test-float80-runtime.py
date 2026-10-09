#!/usr/bin/env python3
"""Exercise actual SR x87 helper signatures with raw extended stack state."""
import argparse
import ctypes,hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser();parser.add_argument('--library',type=pathlib.Path);parser.add_argument('--output',type=pathlib.Path,default=R/'generated/softfloat/runtime');args=parser.parse_args()
out=args.output;out.mkdir(parents=True,exist_ok=True)
libpath=args.library or R/'generated/softfloat/stack/libstack.dylib'
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('st',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx_active',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
lib=ctypes.CDLL(str(libpath));lib.x87_fninit_void.argtypes=[ctypes.POINTER(CPU)]
rng=random.Random(25034)
values=[(0,0),(0,0x8000),(1<<63,0x3fff),((1<<63)+1,0x3fff),((1<<64)-1,0x3fff),((1<<63)+1,0xbfff),(1<<63,0x3ffe),(1<<63,0x4000),(1<<63,1),((1<<64)-1,0x7ffe)]
values += [(rng.getrandbits(63)|(1<<63),rng.choice([1,0x3ff0,0x3fff,0x4000,0x7ffe])|rng.choice([0,0x8000])) for _ in range(20)]
cases=[(op,0x7f|(pc<<8)|(rc<<10),a,b) for pc in [0,2,3] for rc in range(4) for op in range(5) for a in values for b in values[:10]]
subprocess.run(['clang','-arch','x86_64','-O2',str(R/'tests/ext80_oracle.c'),'-o',str(out/'oracle')],check=True)
request=''.join(f'{op} {cw:x} {a[0]:x} {a[1]:x} {b[0]:x} {b[1]:x}\n' for op,cw,a,b in cases)
reply=subprocess.run(['arch','-x86_64',str(out/'oracle'),'--c1'],input=request,text=True,capture_output=True,check=True)
oracle=[tuple(int(v,16) for v in line.split()) for line in reply.stdout.splitlines()];assert len(oracle)==len(cases)
count=0;failures=[]
for (op,cw,a,b),expected in zip(cases,oracle):
 prefix=['fadd','fsub','fmul','fdiv','fsqrt'][op]
 variants=['st','to_st','p_st'] if op!=4 else ['void']
 for variant in variants:
  name='x87_'+prefix+(variant if variant=='p_st' else '_'+variant)
  fn=getattr(lib,name);fn.argtypes=[ctypes.POINTER(CPU)]+([] if op==4 else [ctypes.c_int])
  c=CPU();lib.x87_fninit_void(ctypes.byref(c));c.top=6;c.tag=0x0fff;c.cw=cw;c.sw=0x4000
  for n in range(9):c.regs[n]=rng.getrandbits(32)
  for n in range(6):c.st[n]=rng.getrandbits(64);c.exp[n]=rng.randrange(65536)
  if variant in ['to_st','p_st']:c.st[6],c.exp[6]=b;c.st[7],c.exp[7]=a;target=7
  else:c.st[6],c.exp[6]=a;c.st[7],c.exp[7]=b;target=6
  before=bytes(c.regs),list(c.st[:6]),list(c.exp[:6])
  fn(ctypes.byref(c),*([1] if op!=4 else []))
  actual=c.st[target],c.exp[target],c.sw&0x23f
  if actual!=expected and len(failures)<10:failures.append({'helper':name,'cw':hex(cw),'a':a,'b':b,'actual':actual,'expected':expected})
  assert before==(bytes(c.regs),list(c.st[:6]),list(c.exp[:6])),'helper changed unrelated state'
  assert c.sw&0x4000,'helper discarded sticky condition state'
  assert c.top==(7 if variant=='p_st' else 6)
  if variant=='p_st':assert (c.tag>>(2*6))&3==3
  count+=1
# Native MMX shares physical significands, even after EMMS clears all tags.
lib.tb_mmx_low.argtypes=[ctypes.POINTER(CPU),ctypes.c_uint];lib.tb_mmx_emms.argtypes=[ctypes.POINTER(CPU)]
c=CPU();lib.x87_fninit_void(ctypes.byref(c));c.st[3]=0x123456789abcdef0
assert lib.tb_mmx_low(ctypes.byref(c),3)&0xffffffff==0x9abcdef0
assert c.tag==0 and c.top==0 and c.mmx_active==1 and list(c.exp)==[0xffff]*8
lib.tb_mmx_emms(ctypes.byref(c));assert c.tag==0xffff and c.mmx_active==0 and c.exp[3]==0xffff
lib.x87_fld1_void(ctypes.byref(c));assert (c.st[7],c.exp[7],c.top)==(1<<63,0x3fff,7)
report={'pass':not failures,'cases':count,'failures':failures,'coverage':'SR register, destination-register and pop helper signatures; all rounding/precision controls including C1; physical MMX alias/EMMS transition','library_sha256':hashlib.sha256(libpath.read_bytes()).hexdigest(),'limits':['Memory/integer and transcendental helper families need more coverage','Whole native game has not run']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert not failures
