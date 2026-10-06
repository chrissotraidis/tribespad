#!/usr/bin/env python3
"""Required variable ROR: original sites, every register width and address aliases."""
import ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import pefile,capstone
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import *
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/ror';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
baseline=R/'generated/integration-review/atomic-xor-checkpoint-20261002/generated/integration-review/xor-lifter-gaps.json';gaps=json.loads(baseline.read_text());records=[]
for row in gaps['sites']:
 if row['mnemonic']=='ror':
  raw=bytes.fromhex(row['bytes_hex']);pc=int(row['pc'],16);assert pe.get_data(pc-0x10000000,len(raw))==raw;ins=next(md.disasm(raw,pc));assert ins.op_str.endswith(', cl');records.append(dict(bytes_hex=raw.hex(),pc=pc))
original_count=len(records)
for width in [8,16,32]:
 prefix=b'\x66' if width==16 else b'';opcode=0xd2 if width==8 else 0xd3
 for reg in range(8):records.append(dict(bytes_hex=(prefix+bytes([opcode,0xc8|reg])).hex(),pc=None))
 for tail in [b'\x48\x01',b'\x4c\x24\x03',b'\x4c\xb3\x07']:
  records.append(dict(bytes_hex=(prefix+bytes([opcode])+tail).hex(),pc=None))
srw=R/'generated/tools/SRW-tribes/SRW.exe';run=fixture(out/'fixture',records,original,srw);assert not run.returncode,run.stderr
negative=[]
for pfx in ['f0','64','67','f2','f3']:
 run=fixture(out/('reject-'+pfx),[dict(bytes_hex=pfx+'d308')],original,srw);assert run.returncode!=0,pfx;negative.append(pfx)
procs=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',(out/'fixture/seg01_code.llinc').read_text(),re.M|re.S));assert len(procs)==len(records)
for m in procs:assert 'call tb_shift_' in m[2]
(out/'blocks.llasm').write_text('include tribes_shift.llinc\n'+''.join('proc ror_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procs))
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True,capture_output=True)
s=(out/'blocks.ll').read_text().replace('define hidden fastcc void @ror_','define ccc void @ror_');(out/'blocks.ll').write_text(s)
subprocess.run(['clang','-arch','arm64','-O2','-Wno-override-module','-DPTROFS_64BIT','-dynamiclib',str(out/'blocks.ll'),str(R/'port/core/shift.c'),str(R/'port/llasm-support/tribes_shift.c'),'-o',str(out/'blocks.dylib')],check=True)
lib=ctypes.CDLL(str(out/'blocks.dylib'));u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,4096);u.mem_map(0x200000,65536);memory=ctypes.create_string_buffer(65536);offset=ctypes.addressof(memory)-0x200000;rng=random.Random(25034)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS];names=['eax','ecx','edx','ebx','esp','ebp','esi','edi'];cases=0
for idx,row in enumerate(records):
 raw=bytes.fromhex(row['bytes_hex']);ins=next(md.disasm(raw,0x10000));fn=getattr(lib,'ror_'+str(idx));fn.argtypes=[ctypes.c_void_p];op=ins.operands[0];width=op.size*8;mask=(1<<width)-1
 for n in range(256):
  for pattern in [0,1,mask,1<<(width-1),(1<<(width-1))-1,rng.getrandbits(width)]:
   values=[rng.getrandbits(32) for _ in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884])];values[1]=(values[1]&~255)|n;address=None
   if op.type==capstone.x86.X86_OP_REG:
    name=ins.reg_name(op.reg);selector=['eax','ecx','edx','ebx','esp','ebp','esi','edi'].index(name) if width==32 else ['ax','cx','dx','bx','sp','bp','si','di'].index(name) if width==16 else ['al','cl','dl','bl','ah','ch','dh','bh'].index(name);high=width==8 and selector>=4;selector=selector-4 if high else selector;shift=8 if high else 0
    values[selector]=(values[selector]&~(mask<<shift))|(pattern<<shift)
   else:
    m=op.mem;bn=names.index(ins.reg_name(m.base));ix=names.index(ins.reg_name(m.index)) if m.index else None
    if ix is not None:values[ix]=3
    values[bn]=0x201001-m.disp-(values[ix]*m.scale if ix is not None else 0);address=0x201001
    data=b'\xa5'+pattern.to_bytes(op.size,'little')+b'\xa5'*4;u.mem_write(address-1,data);ctypes.memmove(offset+address-1,data,len(data))
   c=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440);struct.pack_into('<9I',c,0,*values);struct.pack_into('<Q',c,112,offset);before=c.raw
   for reg,v in zip(regs,values):u.reg_write(reg,v)
   u.mem_write(0x10000,raw);u.emu_start(0x10000,0x10000+len(raw),count=1);expected=[u.reg_read(reg) for reg in regs];fn(c);actual=list(struct.unpack_from('<9I',c));defined=0xffffffff if (values[1]&31) in [0,1] else 0xfffff7ff
   assert actual[:8]==expected[:8] and (actual[8]&defined)==(expected[8]&defined),(idx,n,pattern,actual,expected);assert c.raw[36:]==before[36:]
   if address is not None:assert ctypes.string_at(offset+address-1,len(data))==bytes(u.mem_read(address-1,len(data)))
   cases+=1
report=dict(passed=True,original_sites=original_count,blocks=len(records),cases=cases,negative_prefixes=negative,baseline_sha256=sha(baseline),srw_sha256=sha(srw),library_sha256=sha(out/'blocks.dylib'),source_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/patches/srw-tribes.patch',R/'port/core/shift.c',R/'port/core/shift.h',R/'port/llasm-support/tribes_shift.c']},scope='Variable-count ROR8/16/32 emitted ARM64 versus x86 for every count, register/CL/CH alias and unaligned memory forms; multi-bit OF undefined and excluded. All unrelated CPU/flags and surrounding memory preserved. Existing immediate ROR lowering is unchanged.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',original_count,'original ROR sites,',cases,'emitted x86 differential cases')
