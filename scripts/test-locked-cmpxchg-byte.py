#!/usr/bin/env python3
"""Reached byte LOCK CMPXCHG: strict lowering, exhaustive flags and checked contention."""
import argparse,ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import capstone,pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import *
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/locked-cmpxchg-byte';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
image=R/'ref/product/IFC22.dll';pe=pefile.PE(str(image));md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
assert pe.get_data(0x7ff60,4)==bytes.fromhex('f00fb00e')
records=[dict(bytes_hex='f00fb00e',pc=0x1007ff60)]
for src in range(8):
 for tail in [bytes([src<<3]),bytes([0x44|src<<3,0x24,0x11]),bytes([0x44|src<<3,0xb3,0xf1]),bytes([0x45|src<<3,0x0d]),bytes([0x05|src<<3])+bytes.fromhex('01102000')]:
  records.append(dict(bytes_hex=(b'\xf0\x0f\xb0'+tail).hex(),pc=None))
srw=R/'generated/tools/SRW-tribes/SRW.exe';run=fixture(out/'fixture',records,image,srw);assert run.returncode==0,run.stderr
neg=[]
for raw in ['0fb00e','f00fb0ce','66f00fb10e','67f00fb00e','64f00fb00e','f2f00fb00e','f3f00fb00e']:
 run=fixture(out/('reject-'+raw),[dict(bytes_hex=raw)],image,srw);assert run.returncode!=0,raw;neg.append(raw)
code=(out/'fixture/seg01_code.llinc').read_text();procs=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',code,re.M|re.S));assert len(procs)==len(records)
(out/'blocks.llasm').write_text('include tribes_atomic.llinc\n'+''.join('proc bytecas_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procs))
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True,capture_output=True)
s=(out/'blocks.ll').read_text().replace('define hidden fastcc void @bytecas_','define ccc void @bytecas_');(out/'blocks.ll').write_text(s)
helper=R/'port/llasm-support/tribes_atomic.c';libpath=out/'blocks.dylib'
subprocess.run(['clang','-arch','arm64','-O2','-Wno-override-module','-DPTROFS_64BIT','-dynamiclib',str(out/'blocks.ll'),str(helper),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));mem=ctypes.create_string_buffer(65536);guest=0x200000;offset=ctypes.addressof(mem)-guest
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(guest,65536);u.mem_map(0x10000,4096)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS];names=['eax','ecx','edx','ebx','esp','ebp','esi','edi'];rng=random.Random(25034);cases=0
edges=[0,1,0xf,0x10,0x7f,0x80,0xff,0x100,0x7fffffff,0x80000000,0xffffffff,0xfffffffe]
for index,row in enumerate(records):
 raw=bytes.fromhex(row['bytes_hex']);ins=next(md.disasm(raw,0x10000));op=ins.operands[0].mem;fn=getattr(lib,'bytecas_'+str(index));fn.argtypes=[ctypes.c_void_p]
 # Exhaust every AL/destination pair at the actual original instruction; vary
 # register aliases and addressing in all generated source-byte forms.
 iterations=65536 if index==0 else 256
 for k in range(iterations):
  values=[rng.getrandbits(32) for _ in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884])]
  if index==0:values[0]=(values[0]&0xffffff00)|(k&255)
  bn=names.index(ins.reg_name(op.base)) if op.base else None;ix=names.index(ins.reg_name(op.index)) if op.index else None
  if ix is not None:values[ix]=rng.randrange(256)*4
  if bn is not None:values[bn]=(guest+0x1001-op.disp-(values[ix]*op.scale if ix is not None else 0))&0xffffffff
  address=(op.disp+(values[bn] if bn is not None else 0)+(values[ix]*op.scale if ix is not None else 0))&0xffffffff
  assert guest<=address<guest+65536
  value=k>>8 if index==0 else (values[0]&255 if k%2 else rng.randrange(256));data=bytes([value])
  c=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440);struct.pack_into('<9I',c,0,*values);struct.pack_into('<Q',c,112,offset);before=c.raw
  for reg,v in zip(regs,values):u.reg_write(reg,v)
  sentinel=bytes([0xa5,value,0x5a]);u.mem_write(address-1,sentinel);ctypes.memmove(offset+address-1,sentinel,3);u.mem_write(0x10000,raw);u.emu_start(0x10000,0x10000+len(raw),count=1);expected=[u.reg_read(reg) for reg in regs];fn(c)
  actual=list(struct.unpack_from('<9I',c,0));assert actual==expected,(index,k,actual,expected)
  assert c.raw[36:]==before[36:] and ctypes.string_at(offset+address-1,3)==bytes(u.mem_read(address-1,3));cases+=1
# A separate process tests the actual compiler-instrumented helper/blocks.
tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();plugin=R/'generated/tools/guest-memory-pass/GuestMemoryPass.dylib'
subprocess.run([str(tools/'clang'),'-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-S','-emit-llvm',str(helper),'-o',str(out/'helper.ll')],check=True)
for name in ['blocks','helper']:
 subprocess.run([str(tools/'opt'),'-load-pass-plugin',str(plugin),'-passes=tribes-guest-memory,verify',str(out/(name+'.ll')),'-o',str(out/(name+'.bc'))],check=True,capture_output=True)
 subprocess.run([str(tools/'clang'),'-target','arm64-apple-macos12','-O0','-Wno-override-module','-c',str(out/(name+'.bc')),'-o',str(out/(name+'.o'))],check=True)
assert 'atomicrmw add' in (out/'helper.ll').read_text() and 'seq_cst' in (out/'helper.ll').read_text()
outputs={}
for tsan in [False,True]:
 objects=[out/'blocks.o',out/'helper.o']
 if tsan:
  objects=[]
  for name in ['blocks','helper']:
   obj=out/(name+'-tsan.o');subprocess.run([str(tools/'clang'),'-target','arm64-apple-macos12','-O0','-fsanitize=thread','-Wno-override-module','-c',str(out/(name+'.bc')),'-o',str(obj)],check=True);objects.append(obj)
  symbols=subprocess.check_output([str(tools/'llvm-nm'),str(objects[1])],text=True);assert '__tsan_atomic32_fetch_add' in symbols
  assert '__tsan_atomic8_compare_exchange' in symbols
 test=R/'tests/locked_cmpxchg_byte_test.c'
 exe=out/('contention-tsan' if tsan else 'contention');cmd=['clang','-arch','arm64','-O1','-g','-DPTROFS_64BIT']+(['-fsanitize=thread'] if tsan else [])+list(map(str,objects))+[str(R/'port/core/guest_memory_checks.c'),str(test),'-o',str(exe)]
 subprocess.run(cmd,check=True);run=subprocess.run([str(exe)]+(['no-fork'] if tsan else []),capture_output=True,text=True,timeout=90);assert run.returncode==0,(run.returncode,run.stdout,run.stderr);outputs[exe.name]=dict(output=run.stdout,stderr=run.stderr,binary_sha256=sha(exe))
report=dict(passed=True,operation='CMPXCHG byte',original_sites=1,blocks=len(records),differential_cases=cases,negative_encodings=neg,contention=outputs,records=records,library_sha256=sha(libpath),source_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),helper,R/'port/llasm-support/tribes_atomic.llinc',test,R/'scripts/patches/srw-tribes.patch',R/'port/core/guest_memory_checks.c']},scope='Original byte LOCK CMPXCHG plus all eight byte source registers/address forms; exhaustive AL/destination flags reference at original site; native emitted contention and no-mutation guard faults. No new wider/segment/unlocked forms or gameplay acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'x86 differential cases;',outputs)
