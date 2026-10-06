#!/usr/bin/env python3
"""Original allocator BSF/BTR register forms against an independent x86 engine."""
import ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import capstone,pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import *
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/register-bits';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original))
baseline=R/'generated/integration-review/cmpxchg8b-lifter-gaps.json'
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
records=[]
for row in json.loads(baseline.read_text())['sites']:
 if row['mnemonic'] in ['bsf','btr','bt']:
  raw=bytes.fromhex(row['bytes_hex']);pc=int(row['pc'],16)
  ins=next(md.disasm(raw,pc))
  if ins.operands[0].type!=capstone.x86.X86_OP_REG:continue
  assert pe.get_data(pc-0x10000000,len(raw))==raw
  records.append(dict(bytes_hex=raw.hex(),pc=row['pc']))
original_count=len(records);assert original_count==32
for dst in range(8):
 for src in range(8):
  records.append(dict(bytes_hex=bytes([15,0xbc,0xc0|(dst<<3)|src]).hex()))
  records.append(dict(bytes_hex=bytes([15,0xb3,0xc0|(src<<3)|dst]).hex()))
  records.append(dict(bytes_hex=bytes([15,0xa3,0xc0|(src<<3)|dst]).hex()))
 for index in [0,1,31,32,63,127,128,255]:
  records.append(dict(bytes_hex=bytes([15,0xba,0xf0|dst,index]).hex()))
  records.append(dict(bytes_hex=bytes([15,0xba,0xe0|dst,index]).hex()))
srw=R/'generated/tools/SRW-tribes/SRW.exe'
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,run.stderr
# Memory, 16-bit and LOCK-prefixed forms remain unsupported. Segment prefixes
# on register instructions may be discarded by the decoder, as x86 ignores them.
negative=['0fbc00','0fb308','660fbcc0','660fb3c0','f00fbcc0','f00fb3c0','660fa3c0','f00fa3c0']
for raw in negative:
 run=fixture(out/('reject-'+raw),[dict(bytes_hex=raw)],original,srw)
 assert run.returncode!=0,raw
procs=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',(out/'fixture/seg01_code.llinc').read_text(),re.M|re.S))
assert len(procs)==len(records)
for m in procs:assert 'call tb_bs' in m[2] or 'call tb_btr32' in m[2] or 'call tb_bt32' in m[2]
(out/'blocks.llasm').write_text('include tribes_scan.llinc\n'+''.join('proc bits_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procs))
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True,capture_output=True)
(out/'blocks.ll').write_text((out/'blocks.ll').read_text().replace('define hidden fastcc void @bits_','define ccc void @bits_'))
subprocess.run(['clang','-arch','arm64','-O2','-Wno-override-module','-DPTROFS_64BIT','-dynamiclib',str(out/'blocks.ll'),str(R/'port/llasm-support/tribes_scan.c'),'-o',str(out/'blocks.dylib')],check=True)
lib=ctypes.CDLL(str(out/'blocks.dylib'));u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,4096)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS]
names=['eax','ecx','edx','ebx','esp','ebp','esi','edi'];rng=random.Random(25034);cases=0;zero_cases=0
patterns=[0,1,0xffffffff,0x80000000,0x7fffffff]+[1<<i for i in range(32)]+[rng.getrandbits(32) for _ in range(32)]
for i,row in enumerate(records):
 raw=bytes.fromhex(row['bytes_hex']);ins=next(md.disasm(raw,0x10000))
 dst=names.index(ins.reg_name(ins.operands[0].reg));src=ins.operands[1]
 fn=getattr(lib,'bits_'+str(i));fn.argtypes=[ctypes.c_void_p]
 for pattern in patterns:
  for index in [0,1,31,32,33,63,255,0x80000000,0xffffffff,rng.getrandbits(32)]:
   values=[rng.getrandbits(32) for _ in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884])]
   values[dst]=pattern
   if src.type==capstone.x86.X86_OP_REG:
    selector=names.index(ins.reg_name(src.reg));values[selector]=pattern if ins.mnemonic=='bsf' else index
   source=values[selector] if src.type==capstone.x86.X86_OP_REG else src.imm
   c=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440);struct.pack_into('<9I',c,0,*values);before=c.raw
   for reg,value in zip(regs,values):u.reg_write(reg,value)
   u.mem_write(0x10000,raw);u.emu_start(0x10000,0x10000+len(raw),count=1)
   expected=[u.reg_read(reg) for reg in regs];fn(c);actual=list(struct.unpack_from('<9I',c))
   # BSF defines ZF; BTR defines CF and leaves ZF unaffected.
   defined=0xfffff72a|(0x40 if ins.mnemonic=='bsf' else 0x41)
   if ins.mnemonic=='bsf' and source==0:
    assert actual[dst]==values[dst] # implementation policy, not an ISA requirement
    expected[dst]=actual[dst];zero_cases+=1
   assert actual[:8]==expected[:8] and (actual[8]&defined)==(expected[8]&defined),(i,values,actual,expected)
   changed=0x40 if ins.mnemonic=='bsf' else 1
   assert (actual[8]&~changed)==(values[8]&~changed)
   assert c.raw[36:]==before[36:];cases+=1
report=dict(passed=True,original_sites=original_count,blocks=len(records),cases=cases,zero_input_cases=zero_cases,negative_encodings=negative,baseline_sha256=sha(baseline),srw_sha256=sha(srw),library_sha256=sha(out/'blocks.dylib'),source_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/patches/srw-tribes.patch',R/'port/llasm-support/tribes_scan.c',R/'port/llasm-support/tribes_scan.llinc']},scope='BSF32, BTR32 and BT32 register forms, every GPR/source alias, zero and each singleton input, wrapped register/immediate bit indices. Defined flags compared with original x86; undefined arithmetic flags preserved by policy. Zero BSF destination preservation is implementation policy. Complete unrelated CPU preservation.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS:',original_count,'original BSF/BTR/BT sites,',cases,'emitted ARM64 versus x86 cases')
