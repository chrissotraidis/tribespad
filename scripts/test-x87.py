#!/usr/bin/env python3
"""Actual FTST lowering + tag/status transitions, with independent x86 oracles."""
import argparse,ctypes,json,pathlib,re,struct,subprocess,hashlib,random
import hashlib
import pefile
from capstone import *
from unicorn import *
from unicorn.x86_const import *
R=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args();out=R/'generated/x87';out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((R/'inputs-manifest.json').read_text());expected=next(m['sha256'] for m in manifest['modules'] if m['path']=='ref/control/Tribes2.exe')
assert hashlib.sha256((R/'ref/control/Tribes2.exe').read_bytes()).hexdigest()==expected,'original image identity mismatch'
assert json.loads((a.lift/'result.json').read_text())['image_sha256']==expected,'lift image identity mismatch'
pe=pefile.PE(str(R/'ref/control/Tribes2.exe'));image=pe.get_memory_mapped_image();md=Cs(CS_ARCH_X86,CS_MODE_32);actual=[]
for (addr,) in struct.iter_unpack('<I',(R/'generated/analysis/tribes2/instructions.u32').read_bytes()):
 ins=next(md.disasm(image[addr-0x400000:addr-0x400000+15],addr),None)
 if ins and ins.mnemonic=='ftst':actual.append(ins)
blocks=[]
for chunk in (a.lift/'seg01_code.llinc').read_text().split('\n\n'):
 lines=chunk.splitlines();start=next((i for i,l in enumerate(lines) if l==';ftst'),None)
 if start is not None:blocks.append('\n'.join(lines[start+1:]))
assert len(blocks)==len(actual)==34
ll='include tribes_x87.llinc\n'
for i,body in enumerate(blocks):ll+=f'proc test_{i} public\n{body}\nendp\n'
(out/'blocks.llasm').write_text(ll)
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-O2','-I',str(R/'port/llasm-support'),'-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True)
ir=(out/'blocks.ll').read_text().replace('define hidden fastcc void @test_','define ccc void @test_');(out/'blocks.ll').write_text(ir)
subprocess.run(['clang','-arch','arm64','-O2','-DPTROFS_64BIT','-dynamiclib',str(out/'blocks.ll'),str(R/'port/core/x87_status.c'),str(R/'port/core/mmx.c'),str(R/'port/llasm-support/tribes_x87.c'),str(R/'port/llasm-support/tribes_mmx.c'),str(R/'port/llasm-support/llasm_float.c'),str(R/'tests/x87_result_slot.c'),'-o',str(out/'libx87.dylib')],check=True)
subprocess.run(['clang','-arch','x86_64','-O2',str(R/'tests/x87_oracle.c'),'-o',str(out/'oracle-x86')],check=True)
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('st',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx_active',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
class Ext80(ctypes.Structure):_fields_=[('m',ctypes.c_uint64),('e',ctypes.c_uint16)]
lib=ctypes.CDLL(str(out/'libx87.dylib'));lib.tb_x87_ftst80.argtypes=[Ext80,ctypes.c_uint16,ctypes.c_uint16,ctypes.c_int];lib.tb_x87_ftst80.restype=ctypes.c_uint16;lib.tb_x87_from_binary64.argtypes=[ctypes.c_uint64];lib.tb_x87_from_binary64.restype=Ext80
rng=random.Random(25034);bits=[0,1<<63,0x3ff0000000000000,0xbff0000000000000,0x7ff0000000000000,0xfff0000000000000,0x7ff8000000000000,0xfff8000000000001,0x7ff0000000000001,1,0x8000000000000001,0x0010000000000000,0x7fefffffffffffff]+[rng.getrandbits(64) for _ in range(48)]
scenarios=[(0,top,b) for top in range(8) for b in bits]+[(1,top,0) for top in range(8)]+[(mode,0,b) for mode in [3] for b in bits[:13]]
request=''.join(f'{mode} {top} {b:016x}\n' for mode,top,b in scenarios)
r=subprocess.run(['arch','-x86_64',str(out/'oracle-x86')],input=request,text=True,capture_output=True,check=True);oracle=[tuple(int(x,16) for x in l.split()) for l in r.stdout.splitlines()];assert len(oracle)==len(scenarios)
count=0
for i,body in enumerate(blocks):
 fn=getattr(lib,f'test_{i}');fn.argtypes=[ctypes.POINTER(CPU)]
 for (mode,top,b),(before,after) in zip(scenarios,oracle):
  c=CPU();c.top=(before>>11)&7;c.sw=before&~0x3800;c.cw=0x37f;c.tag=0xffff if mode in [1,3] else 0;c.mmx_active=mode==2;c.st[c.top]=b
  for n in range(9):c.regs[n]=rng.getrandbits(32)
  preserved=bytes(c.regs),bytes(c.st),c.top,c.cw,c.tag,c.mmx_active
  fn(ctypes.byref(c));status=c.sw|(c.top<<11)
  assert status==after,(i,mode,top,hex(b),hex(before),hex(status),hex(after))
  assert preserved==(bytes(c.regs),bytes(c.st),c.top,c.cw,c.tag,c.mmx_active),'FTST changed other state';count+=1
# Rosetta does not update x87 physical tags on MMX instructions. Validate the
# Intel-specified MMX exponent/tag transition independently of that oracle.
for b in bits[:13]:
 c=CPU();c.cw=0x37f;c.st[0]=b;c.mmx_active=1;c.tag=0
 lib.tb_x87_ftst(ctypes.byref(c))
 assert c.sw==(0x100 if b==0x8000000000000000 else 0x4501)
# Raw extended80 denormal, pseudo-denormal, unsupported format, masked NaN,
# stack fault, and unmasked deferred exception, derived from Intel SDM FTST.
core_cases=[(Ext80(0,0),0,0x37f,0,0x4000),(Ext80(0,0x8000),0,0x37f,0,0x4000),(Ext80(1,0),0,0x37f,0,2),(Ext80(1,0x8000),0,0x37f,0,0x102),(Ext80(0x8000000000000000,0),0,0x37f,0,2),(Ext80(1,1),0,0x37f,0,0x4501),(Ext80(0xc000000000000001,0x7fff),0,0x37f,0,0x4501),(Ext80(0x8000000000000000,0x3fff),0x202,0x37f,1,0x4543),(Ext80(1,0),0x4000,0x37d,0,0xc082),(Ext80(1,1),0x4000,0x37e,0,0xc081)]
for v,sw,cw,empty,expected in core_cases:assert lib.tb_x87_ftst80(v,sw,cw,empty)==expected,(hex(v.m),hex(v.e),'extended classification')
# Tag and sticky exception transitions use the same native runtime the client will.
for name in ['x87_fninit_void','x87_fldz_void','x87_fld1_void','x87_fstp_st','tb_x87_ftst','tb_x87_fclex','tb_mmx_emms','tb_x87_ffree']:getattr(lib,name).argtypes=[ctypes.POINTER(CPU)]+([ctypes.c_uint] if name in ['x87_fstp_st','tb_x87_ffree'] else [])
c=CPU();lib.x87_fninit_void(ctypes.byref(c));assert (c.tag,c.top,c.cw)==(0xffff,0,0x37f)
lib.tb_x87_ftst(ctypes.byref(c));assert c.sw==0x4541
lib.tb_x87_fclex(ctypes.byref(c));assert c.sw==0x4500
lib.x87_fldz_void(ctypes.byref(c));lib.tb_x87_ftst(ctypes.byref(c));assert c.sw==0x4000 and c.top==7 and ((c.tag>>14)&3)!=3
lib.x87_fld1_void(ctypes.byref(c));lib.tb_x87_ftst(ctypes.byref(c));assert c.sw==0 and c.top==6
lib.tb_x87_ffree(ctypes.byref(c),0);lib.tb_x87_ftst(ctypes.byref(c));assert c.sw==0x4541
lib.tb_x87_fclex(ctypes.byref(c));lib.tb_mmx_emms(ctypes.byref(c));assert c.tag==0xffff
lib.x87_fld1_void(ctypes.byref(c));lib.tb_x87_ftst(ctypes.byref(c));assert c.sw==0 and c.top==5
c.sw|=1;lib.x87_fldz_void(ctypes.byref(c));assert c.sw&1,'FLDZ lost sticky IE'
result={'pass':True,'actual_ftst_sites':34,'arm64_emitted_cases':count,'raw_extended80_status_cases':len(core_cases),'oracle':'x86_64 FTST through Rosetta; Intel SDM revision 089 page 1089 for exceptional extended80 cases','limits':['Rosetta and Unicorn do not validate unmasked exception delivery or MMX/x87 tag aliasing','Runtime fails closed on an unserviced x87 exception','Inherited stack still stores binary64; full extended80 arithmetic pending','Tag classification and all x87 producers/consumers require further validation'],'binary_sha256':hashlib.sha256((out/'libx87.dylib').read_bytes()).hexdigest(),'lift':str(a.lift)};(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
