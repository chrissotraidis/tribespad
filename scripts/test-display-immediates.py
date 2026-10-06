#!/usr/bin/env python3
"""Compare full dynamic display MOV blocks to independently patched x86."""
import ctypes,hashlib,json,mmap,pathlib,random,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/display-immediates-candidate';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();meta=json.loads((out/'preparation.json').read_text());assert meta['passed'] and not meta['published']
for n,h in meta['inputs_sha256'].items():assert sha(R/n)==h,n
for n,h in meta['outputs_sha256'].items():assert sha(out/n)==h,n
s=(out/'engine.ll').read_text();bridge='@observed_target = global i32 0\n'
for row in meta['records']:
 name=row['name'];s=s.replace('@'+name+'(','@candidate_'+name+'(')
 bridge+='define hidden fastcc void @loc_%X(ptr %%cpu) { store i32 %d, ptr @observed_target\n ret void\n}\n'%(row['target'],row['target'])
 bridge+='declare hidden fastcc void @candidate_%s(ptr)\ndefine void @enter_%s(ptr %%cpu) { call fastcc void @candidate_%s(ptr %%cpu)\n ret void\n}\n'%(name,name,name)
(out/'isolated.ll').write_text(s);(out/'bridge.ll').write_text(bridge);libpath=out/'test.dylib'
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(out/'isolated.ll'),str(out/'bridge.ll'),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));observed=ctypes.c_uint32.in_dll(lib,'observed_target');memory=mmap.mmap(-1,0x600000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];rng=random.Random(0x561251);cases=0
values=[0,1,600,800,1080,1920,1710,1068,2048,2732,0x7fffffff,0x80000000,0xffffffff]+[rng.getrandbits(32) for _ in range(243)]
for row in meta['records']:
 u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x561000,4096);fn=getattr(lib,'enter_'+row['name']);fn.argtypes=[ctypes.c_void_p]
 for value in values:
  page=bytearray(rng.getrandbits(4096*8).to_bytes(4096,'little'));offset=row['entry']-0x561000;page[offset:offset+5]=b'\xb8'+value.to_bytes(4,'little');u.mem_write(0x561000,bytes(page));ctypes.memmove(base+0x561000,bytes(page),4096)
  state=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);v=[rng.getrandbits(32) for _ in range(8)]+[0x202|sum(b for b in [1,4,16,64,128,1024,2048] if rng.getrandbits(1))];struct.pack_into('<9I',state,0,*v);struct.pack_into('<Q',state,112,base);expected=bytearray(state.raw)
  for reg,x in zip(regs,v):u.reg_write(reg,x)
  u.emu_start(row['entry'],row['target'],count=1);assert u.reg_read(X.UC_X86_REG_EIP)==row['target'];struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);observed.value=0;fn(state)
  assert state.raw==expected and observed.value==row['target'];assert struct.unpack_from('<I',state,0)[0]==value;assert ctypes.string_at(base+0x561000,4096)==bytes(page)==bytes(u.mem_read(0x561000,4096));cases+=1
report=dict(passed=True,published=False,cases=cases,definitions=2,cpu_bytes=440,preparation_sha256=sha(out/'preparation.json'),sources_sha256={str(pathlib.Path(__file__).resolve().relative_to(R)):sha(pathlib.Path(__file__).resolve())},outputs_sha256={n:sha(out/n) for n in ['isolated.ll','bridge.ll','test.dylib']},scope='512 ARM64 versus x86 whole-block MOV checks across realistic display sizes and full32-bit values; CPU, flags, next target and entire immediate page preserved. Runtime activation and original writer remain unverified.')
(out/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'dynamic MOV cases,440 CPU bytes and immediate page unchanged')
