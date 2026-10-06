#!/usr/bin/env python3
"""Execute real lifted trampolines up to their original-image transfer targets."""
import ctypes,hashlib,json,pathlib,random,struct,subprocess
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/followup-trampolines';meta=json.loads((out/'result.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();assert sha(out/'module.o')==meta['object_sha256'] and sha(out/'module.ll')==meta['module_sha256']
plan=json.loads((R/meta['plan']).read_text());assert sha(R/meta['plan'])==meta['plan_sha256'];entries=json.loads((out/'dispatch-entries.json').read_text());targets=set(meta['external_targets'])
assert sha(out/'dispatch-entries.json')==meta['dispatch_entries_sha256']
bridge=['@observed_target = global i32 0']
for target in sorted(targets):bridge.append('define hidden fastcc void @tbpatch_target_%08x(ptr %%cpu) { store i32 %d, ptr @observed_target\n ret void\n}'%(target,target))
for i,row in enumerate(entries):bridge+=['declare fastcc void @%s(ptr)'%row['symbol'],'define void @enter_%d(ptr %%cpu) { call fastcc void @%s(ptr %%cpu)\n ret void\n}'%(i,row['symbol'])]
(out/'test-bridge.ll').write_text('\n'.join(bridge)+'\n');subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(out/'module.o'),str(out/'test-bridge.ll'),'-o',str(out/'test.dylib')],check=True)
lib=ctypes.CDLL(str(out/'test.dylib'));observed=ctypes.c_uint32.in_dll(lib,'observed_target');u=Uc(UC_ARCH_X86,UC_MODE_32);bank=plan['native_allocation']['address'];u.mem_map(bank,65536)
path=R/plan['bank_path'];assert sha(path)==plan['bank_sha256'];u.mem_write(bank,path.read_bytes())
# Trap-filled observer pages bound Unicorn decoding; callbacks stop before INT3.
for page in sorted({target&~4095 for target in targets}):u.mem_map(page,4096);u.mem_write(page,b'\xcc'*4096)
u.mem_map(0x200000,65536);memory=ctypes.create_string_buffer(65536);offset=ctypes.addressof(memory)-0x200000;expected_target=[0]
def transfer(uc,address,size,data):
 if address in targets:expected_target[0]=address;uc.emu_stop()
u.hook_add(UC_HOOK_CODE,transfer)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS];rng=random.Random(25034);cases=0
for i,row in enumerate(entries):
 fn=getattr(lib,'enter_%d'%i);fn.argtypes=[ctypes.c_void_p]
 for sample in range(256):
  pattern=bytes(rng.getrandbits(8) for _ in range(256))*256;u.mem_write(0x200000,pattern);ctypes.memmove(memory,pattern,65536)
  values=[rng.getrandbits(32) for _ in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884,0x400])];values[4]=0x208000+(sample%16)*4
  cpu=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,offset);before=cpu.raw
  for reg,value in zip(regs,values):u.reg_write(reg,value)
  expected_target[0]=0;u.emu_start(row['address'],0,count=64);assert expected_target[0] in targets
  expected=[u.reg_read(reg) for reg in regs];observed.value=0;fn(cpu)
  assert observed.value==expected_target[0],(row,sample,observed.value,expected_target)
  assert list(struct.unpack_from('<9I',cpu))==expected,(row,sample)
  assert cpu.raw[36:]==before[36:] and memory.raw==bytes(u.mem_read(0x200000,65536)),(row,sample,'state/memory')
  cases+=1
result=dict(passed=True,entries=len(entries),trampolines=meta['trampolines'],cases=cases,targets=sorted(targets),plan_sha256=meta['plan_sha256'],module_sha256=meta['module_sha256'],binary_sha256=sha(out/'test.dylib'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Actual compiled ARM64 trampoline prefixes  compared with original x86. All GPRs/flags, complete 64 KiB stack region and unrelated CPU state agree at each external transfer. Original-image callees/continuations are observed boundaries, not executed here; no dispatch publication or patched game claim.')
(out/'test-result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS:',cases,'native/x86 cases across',len(entries),'trampoline/return entries with exact control-flow targets and complete stack/CPU comparison')
