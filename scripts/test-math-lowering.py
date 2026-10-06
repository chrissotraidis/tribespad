#!/usr/bin/env python3
"""Test every original transcendental/scale/remainder site's emitted ARM64 ABI."""
import argparse,ctypes,hashlib,json,pathlib,re,struct,subprocess
import pefile
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
R=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args();out=R/'generated/bochs-math/lowering';out.mkdir(parents=True,exist_ok=True)
result=json.loads((a.lift/'result.json').read_text());assert result['exit']==0 and result['full_flags']
names=['f2xm1','fsin','fcos','fptan','fyl2x','fpatan','fprem','fscale'];exe=R/'ref/control/Tribes2.exe';assert hashlib.sha256(exe.read_bytes()).hexdigest()==result['image_sha256']
pe=pefile.PE(str(exe));image=pe.get_memory_mapped_image();md=Cs(CS_ARCH_X86,CS_MODE_32);actual=[]
for (addr,) in struct.iter_unpack('<I',(R/'generated/analysis/tribes2/instructions.u32').read_bytes()):
 ins=next(md.disasm(image[addr-0x400000:addr-0x400000+15],addr),None)
 if ins and ins.mnemonic in names:actual.append((addr,ins.mnemonic))
blocks=[]
for chunk in (a.lift/'seg01_code.llinc').read_text().split('\n\n'):
 lines=chunk.splitlines();start=next((i for i,l in enumerate(lines) if l.startswith(';') and l[1:] in names),None)
 if start is not None:blocks.append((lines[start][1:],'\n'.join(lines[start+1:])))
assert [name for _,name in actual]==[name for name,_ in blocks] and len(blocks)==649
(out/'blocks.llasm').write_text('include llasm_float.llinc\n'+''.join(f'proc actual_{i} public\n{body}\nendp\n' for i,(_,body) in enumerate(blocks)))
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-O2','-I',str(R/'port/llasm-support'),'-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True)
ir=(out/'blocks.ll').read_text().replace('define hidden fastcc void @actual_','define ccc void @actual_');(out/'blocks.ll').write_text(ir)
sources=['port/core/ext80.c','port/llasm-support/tribes_float80.c','tests/x87_result_slot.c']
subprocess.run(['clang','-arch','arm64','-O2','-std=c11','-Wall','-Wextra','-Werror','-Wno-override-module','-DPTROFS_64BIT','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(R/'ref/softfloat/SoftFloat-3e/source/include'),'-dynamiclib',str(out/'blocks.ll'),*[str(R/s) for s in sources],str(R/'generated/softfloat/macos/softfloat.a'),str(R/'generated/bochs-math/libmath.dylib'),'-o',str(out/'liblowering.dylib')],check=True)
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('st',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx_active',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
lib=ctypes.CDLL(str(out/'liblowering.dylib'));lib.x87_fninit_void.argtypes=[ctypes.POINTER(CPU)]
trace=json.loads((R/'generated/bochs-math/tests/trace.json').read_text());cases={op:[row for row in trace if row['op']==op][:16] for op in range(8)}
count=0;independent_exact=0;trans_status_differences=0
for i,(name,_) in enumerate(blocks):
 op=names.index(name);fn=getattr(lib,f'actual_{i}');fn.argtypes=[ctypes.POINTER(CPU)]
 for row in cases[op]:
  c=CPU();lib.x87_fninit_void(ctypes.byref(c));c.top=6;c.tag=0xfff;c.cw=row['cw'];c.st[6],c.exp[6]=row['a'];c.st[7],c.exp[7]=row['b'];c.sw=0x100
  for n in range(9):c.regs[n]=0x25034000+n
  keep=5 if op==3 else 6;preserved=bytes(c.regs),tuple(c.st[:keep]),tuple(c.exp[:keep]);fn(ctypes.byref(c))
  # FPREM can require repeated reductions; single emitted execution is kept for this census.
  expected=row['result'];slot=7 if op in [4,5] else 6
  assert [c.st[slot],c.exp[slot]]==expected,(i,name,row,'numeric helper ABI')
  assert preserved==(bytes(c.regs),tuple(c.st[:keep]),tuple(c.exp[:keep])),'helper clobbered GPRs or unrelated stack contents'
  assert c.top==({3:5,4:7,5:7}.get(op,6))
  if op==3:assert (c.st[5],c.exp[5])==(1<<63,0x3fff) and (c.tag>>(2*5))&3==0
  if op in [4,5]:assert (c.tag>>(2*6))&3==3
  if op in [6,7]:
   mask=row['status_mask'] if not row['flags']&1 else 0x3f;assert (c.sw&mask)==(row['oracle'][2]&mask),(i,name,'x86 status');independent_exact+=1
  else:
   if (c.sw&row['status_mask'])!=(row['oracle'][2]&row['status_mask']):trans_status_differences+=1
  count+=1
report={'abi_pass':True,'original_sites':len(actual),'arm64_cases':count,'independent_exact_remainder_scale_cases':independent_exact,'transcendental_status_differences_from_rosetta':trans_status_differences,'library_sha256':hashlib.sha256((out/'liblowering.dylib').read_bytes()).hexdigest(),'lift':str(a.lift.resolve()),'limits':['First sixteen recorded scenarios per operation; broad corpus is in pure-math evidence','Six operations compare emitted ABI values to the tested math library; independent numeric bounds are separate','Transcendental exception flags, partial reductions and exceptional stack behavior remain open','Whole game has not run']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
