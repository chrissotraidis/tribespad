#!/usr/bin/env python3
"""Preserve a relocated code address as an integer in original XOR lowering."""
import ctypes,hashlib,importlib.util,json,os,pathlib,random,re,struct,subprocess
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import *
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/relocated-xor';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));actual=bytes.fromhex('3550970b10');assert pe.get_data(0xb9755,5)==actual
# Original instruction plus all other GPR destinations. Relocation still names
# the original code address, backed by a separate fixture section for discovery.
records=[actual]+[bytes([0x81,0xf0|i])+struct.pack('<I',0x100b9750) for i in range(8)]
raw=bytearray(original.read_bytes()[:0x400]);head=struct.unpack_from('<I',raw,0x3c)[0];opt=head+24;sec=opt+224
struct.pack_into('<H',raw,head+6,2);struct.pack_into('<H',raw,head+22,0x103)
for off,value in [(4,512),(8,4096),(16,0x1000),(56,0xba000),(60,0x400)]:struct.pack_into('<I',raw,opt+off,value)
raw[opt+96:opt+224]=bytes(128);raw[sec:]=bytes(len(raw)-sec)
struct.pack_into('<8s6I2HI',raw,sec,b'.text\0\0\0',512,0x1000,512,0x400,0,0,0,0,0x60000020)
struct.pack_into('<8s6I2HI',raw,sec+40,b'.rdata\0\0',4096,0xb9000,4096,0x600,0,0,0,0,0x40000040)
code=b''.join(x+b'\xc3'+bytes(31-len(x)) for x in records);raw+=code+bytes(512-len(code))+bytes(4096)
(out/'fixture.exe').write_bytes(raw);(out/'relocations.csv').write_text(''.join('0x%x,0x100b9750,\n'%(0x10001000+32*i+len(x)-4) for i,x in enumerate(records)));(out/'global_aliases.sci').write_text(''.join('loc_%X,fixture_%d\n'%(0x10001000+32*i,i) for i in range(len(records))))
srw=R/'generated/tools/SRW-tribes/SRW.exe';run=subprocess.run([str(srw),'fixture.exe','fixture.llasm'],cwd=out,capture_output=True,text=True,env={k:v for k,v in {**os.environ,'TRIBES_SRW_FULL_FLAGS':'1'}.items() if k!='HALOPAD_SRW_DIAG'});(out/'srw.log').write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
text=(out/'seg01_code.llinc').read_text();procs=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procs)==len(records)
blocks=[]
for m in procs:
 body=m[2].split(';ret\n')[0];assert 'mov tmp1, loc_100B9750' in body and not re.search(r'^load',body,re.M)
 transformed,stats=va.transform(body.splitlines());assert stats['value_labels']==1
 blocks.append('proc xor_'+m[1]+' public\n'+'\n'.join(transformed)+'\nendp\n')
(out/'blocks.llasm').write_text('include llasm.llinc\n'+''.join(blocks))
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True,capture_output=True)
s=(out/'blocks.ll').read_text().replace('define hidden fastcc void @xor_','define ccc void @xor_');(out/'blocks.ll').write_text(s)
subprocess.run(['clang','-arch','arm64','-O2','-Wno-override-module','-dynamiclib',str(out/'blocks.ll'),'-o',str(out/'blocks.dylib')],check=True)
lib=ctypes.CDLL(str(out/'blocks.dylib'));u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,4096);rng=random.Random(25034);regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS];cases=0
for i,raw in enumerate(records):
 fn=getattr(lib,'xor_'+str(i));fn.argtypes=[ctypes.c_void_p];dest=0 if i==0 else i-1
 for value in [0,1,0xffffffff,0x80000000,0x7fffffff,0x100b9750,0xeff468af]+[rng.getrandbits(32) for _ in range(2048)]:
  for flags in [0x202,0xad7,0x643]:
   values=[rng.getrandbits(32) for _ in range(8)]+[flags];values[dest]=value;c=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440);struct.pack_into('<9I',c,0,*values);before=c.raw
   for reg,v in zip(regs,values):u.reg_write(reg,v)
   u.mem_write(0x10000,raw);u.emu_start(0x10000,0x10000+len(raw),count=1);expected=[u.reg_read(reg) for reg in regs];fn(c);result=list(struct.unpack_from('<9I',c));assert result[:8]==expected[:8] and (result[8]&~0x10)==(expected[8]&~0x10),(i,value,flags,result,expected);assert c.raw[36:]==before[36:];cases+=1
report=dict(passed=True,cases=cases,original_pc='0x100b9755',original_bytes=actual.hex(),guest_immediate='0x100b9750',scope='Relocated XOR32 register immediate, all GPR destinations, defined flags and complete unrelated CPU preservation; AF is architecturally undefined. Original guest-address transform used. No guest memory is mapped for the original immediate, so accidental dereference would fail.',sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/prepare-va-model.py',R/'scripts/patches/srw-tribes.patch']},srw_sha256=sha(srw),library_sha256=sha(out/'blocks.dylib'),fixture_sha256=sha(out/'fixture.exe'))
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'relocated XOR emitted ARM64 versus original x86 cases')
