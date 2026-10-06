#!/usr/bin/env python3
"""Required original LOCK XADD32: emitted blocks vs x86 plus checked contention."""
import argparse,ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import capstone,pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import *
from sse_instruction_fixture import fixture
ap=argparse.ArgumentParser();modes=ap.add_mutually_exclusive_group();modes.add_argument('--cmpxchg',action='store_true');modes.add_argument('--cmpxchg8b',action='store_true');args=ap.parse_args();operation='CMPXCHG8B' if args.cmpxchg8b else 'CMPXCHG' if args.cmpxchg else 'XADD';size=8 if args.cmpxchg8b else 4
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review'/('locked-cmpxchg8b' if args.cmpxchg8b else 'locked-cmpxchg' if args.cmpxchg else 'locked-xadd');out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
image=R/'ref/product/IFC22.dll';pe=pefile.PE(str(image));md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
records=([dict(bytes_hex='f00fb10a',pc=None),dict(bytes_hex='f00fc10a',pc=None)] if args.cmpxchg else [dict(bytes_hex='f00fc130',pc=None)]);records=[dict(bytes_hex='f00fc70f',pc=None)] if args.cmpxchg8b else records;synthetic_prefix=len(records)
baseline=R/('generated/integration-review/cmpxchg-lifter-gaps.json' if args.cmpxchg8b else 'generated/integration-review/ror-lifter-gaps.json' if args.cmpxchg else 'generated/integration-review/shift-checkpoint-20261002/generated/integration-review/lifter-gaps.json');gaps=json.loads(baseline.read_text())
for row in gaps['sites']:
 if row['mnemonic']==('lock cmpxchg8b' if args.cmpxchg8b else 'lock cmpxchg' if args.cmpxchg else 'lock xadd'):
  pc=int(row['pc'],16);raw=bytes.fromhex(row['bytes_hex']);assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,len(raw))==raw
  ins=next(md.disasm(raw,pc))
  if ins.operands[0].size!=size:continue
  records.append(dict(bytes_hex=raw.hex(),pc=pc))
original_count=len(records)-synthetic_prefix
if args.cmpxchg8b:
 for tail in [b'\x08',b'\x09',b'\x0a',b'\x0b',b'\x0c\x24',b'\x4d\x00',b'\x0e',b'\x0f',b'\x4c\xb3\xf0',b'\x0d'+bytes.fromhex('00102000')]:
  records.append(dict(bytes_hex=(b'\xf0\x0f\xc7'+tail).hex(),pc=None))
else:
 for src in range(8):
  for tail in [bytes([src<<3]),bytes([0x44|src<<3,0x24,0x10]),bytes([0x44|src<<3,0xb3,0xf0]),bytes([0x45|src<<3,0x0c]),bytes([0x05|src<<3])+bytes.fromhex('00102000')]:
   records.append(dict(bytes_hex=((b'\xf0\x0f\xb1' if args.cmpxchg else b'\xf0\x0f\xc1')+tail).hex(),pc=None))
srw=R/'generated/tools/SRW-tribes/SRW.exe';run=fixture(out/'fixture',records,image,srw);assert run.returncode==0,run.stderr
neg=[]
for raw in (['0fc70f','f00fc7c8','67f00fc70f','64f00fc70f','f2f00fc70f','f3f00fc70f'] if args.cmpxchg8b else ['0fc130','f00fc1f0','f00fc030','66f00fc130','67f00fc130','64f00fc130','f2f00fc130','f3f00fc130']):
 raw=raw.replace('c1','b1').replace('c0','b0') if args.cmpxchg else raw
 if args.cmpxchg and raw=='f00fb030':continue # Byte CMPXCHG is now separately implemented and verified.
 run=fixture(out/('reject-'+raw),[dict(bytes_hex=raw)],image,srw);assert run.returncode!=0,raw;neg.append(raw)
code=(out/'fixture/seg01_code.llinc').read_text();procs=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',code,re.M|re.S));assert len(procs)==len(records)
(out/'blocks.llasm').write_text('include tribes_atomic.llinc\n'+''.join('proc xadd_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procs))
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True,capture_output=True)
s=(out/'blocks.ll').read_text().replace('define hidden fastcc void @xadd_','define ccc void @xadd_');(out/'blocks.ll').write_text(s)
helper=R/'port/llasm-support/tribes_atomic.c';libpath=out/'blocks.dylib'
subprocess.run(['clang','-arch','arm64','-O2','-Wno-override-module','-DPTROFS_64BIT','-dynamiclib',str(out/'blocks.ll'),str(helper),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));mem=ctypes.create_string_buffer(65536);guest=0x200000;offset=ctypes.addressof(mem)-guest
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(guest,65536);u.mem_map(0x10000,4096)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS];names=['eax','ecx','edx','ebx','esp','ebp','esi','edi'];rng=random.Random(25034);cases=0
edges=[0,1,0xf,0x10,0x7f,0x80,0xff,0x100,0x7fffffff,0x80000000,0xffffffff,0xfffffffe]
for index,row in enumerate(records):
 raw=bytes.fromhex(row['bytes_hex']);ins=next(md.disasm(raw,0x10000));op=ins.operands[0].mem;src=3 if args.cmpxchg8b else names.index(ins.reg_name(ins.operands[1].reg));fn=getattr(lib,'xadd_'+str(index));fn.argtypes=[ctypes.c_void_p]
 for k in range(256):
  values=[rng.getrandbits(32) for _ in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884])];values[src]=edges[k%len(edges)] if k<144 else rng.getrandbits(32)
  if args.cmpxchg or args.cmpxchg8b:values[0]=edges[k%len(edges)] if k<144 else rng.getrandbits(32)
  bn=names.index(ins.reg_name(op.base)) if op.base else None;ix=names.index(ins.reg_name(op.index)) if op.index else None
  if ix is not None:values[ix]=rng.randrange(256)*4
  if bn is not None:values[bn]=(guest+0x1000-op.disp-(values[ix]*op.scale if ix is not None else 0))&0xffffffff
  address=(op.disp+(values[bn] if bn is not None else 0)+(values[ix]*op.scale if ix is not None else 0))&0xffffffff
  assert guest<=address<=guest+65536-size and not address%size
  value=edges[(k//len(edges))%len(edges)] if k<144 else rng.getrandbits(32);value=values[0] if args.cmpxchg and ins.mnemonic=='lock cmpxchg' and k%2 else value;value=(((values[2]<<32)|values[0]) if k%2 else rng.getrandbits(64)) if args.cmpxchg8b else value;data=value.to_bytes(size,'little')
  c=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440);struct.pack_into('<9I',c,0,*values);struct.pack_into('<Q',c,112,offset);before=c.raw
  for reg,v in zip(regs,values):u.reg_write(reg,v)
  u.mem_write(address,data);ctypes.memmove(offset+address,data,size);u.mem_write(0x10000,raw);u.emu_start(0x10000,0x10000+len(raw),count=1);expected=[u.reg_read(reg) for reg in regs];fn(c)
  actual=list(struct.unpack_from('<9I',c,0));assert actual==expected,(index,k,actual,expected)
  assert c.raw[36:]==before[36:] and ctypes.string_at(offset+address,size)==bytes(u.mem_read(address,size));cases+=1
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
  if args.cmpxchg:assert '__tsan_atomic32_compare_exchange' in symbols
  if args.cmpxchg8b:assert '__tsan_atomic64_compare_exchange' in symbols
 test=R/'tests'/('locked_cmpxchg8b_test.c' if args.cmpxchg8b else 'locked_cmpxchg_test.c' if args.cmpxchg else 'locked_xadd_test.c')
 exe=out/('contention-tsan' if tsan else 'contention');cmd=['clang','-arch','arm64','-O1','-g','-DPTROFS_64BIT']+(['-fsanitize=thread'] if tsan else [])+list(map(str,objects))+[str(R/'port/core/guest_memory_checks.c'),str(test),'-o',str(exe)]
 subprocess.run(cmd,check=True);run=subprocess.run([str(exe)]+(['no-fork'] if tsan else []),capture_output=True,text=True,timeout=90);assert run.returncode==0,(run.returncode,run.stdout,run.stderr);outputs[exe.name]=dict(output=run.stdout,stderr=run.stderr,binary_sha256=sha(exe))
report=dict(passed=True,operation=operation,original_sites=original_count,baseline_sha256=sha(baseline),blocks=len(records),differential_cases=cases,negative_encodings=neg,contention=outputs,records=records,library_sha256=sha(libpath),source_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),helper,R/'port/llasm-support/tribes_atomic.llinc',test,R/'scripts/patches/srw-tribes.patch',R/'port/core/guest_memory_checks.c']},scope='Required aligned LOCK '+operation+(' memory form only.' if args.cmpxchg8b else '32 memory form only.')+' x86 emulator arithmetic/flags reference, native emitted contention and checked permission faults. Unaligned operands stop explicitly; other widths/forms remain unsupported. No Windows fault-delivery or gameplay acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',original_count,'original sites,',cases,'x86 differential cases;',outputs)
