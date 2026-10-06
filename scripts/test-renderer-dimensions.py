#!/usr/bin/env python3
"""Check the two dimension-source blocks against their original patched x86."""
import ctypes,hashlib,json,mmap,pathlib,random,re,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-dimensions';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();meta=json.loads((out/'preparation.json').read_text());assert meta['passed']
for n,h in meta['inputs_sha256'].items():assert sha(R/n)==h,n
for n,h in meta['outputs_sha256'].items():assert sha(out/n)==h,n
source=(out/'engine.ll').read_text();targets=[r['target'] for r in meta['records']];bridge='@observed_target = global i32 0\n'
for row,target in zip(meta['records'],targets):
 name=row['name'];source=source.replace('define hidden fastcc void @'+name+'(','define hidden fastcc void @candidate_'+name+'(',1)
 bridge+='define hidden fastcc void @loc_%X(ptr %%cpu) { store i32 %d, ptr @observed_target\n ret void\n}\n'%(target,target)
 bridge+='declare hidden fastcc void @candidate_%s(ptr)\ndefine void @enter_%s(ptr %%cpu) { call fastcc void @candidate_%s(ptr %%cpu)\n ret void\n}\n'%(name,name,name)
(out/'isolated.ll').write_text(source);(out/'bridge.ll').write_text(bridge);libpath=out/'test.dylib'
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(out/'isolated.ll'),str(out/'bridge.ll'),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));observed=ctypes.c_uint32.in_dll(lib,'observed_target');memory=mmap.mmap(-1,0x10200000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory))
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];rng=random.Random(0x1005b384);cases=0;visited={}
for row,target in zip(meta['records'],targets):
 u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
 for page in sorted({row['entry']&~4095,target&~4095}):u.mem_map(page,4096)
 u.mem_map(0x101d6000,4096);u.mem_map(0x200000,65536);u.mem_map(0x9e9000,4096);u.mem_write(0x9e9000,bytes(4096));u.mem_write(row['entry'],bytes.fromhex(row['after_bytes']));steps=[];hit=[]
 def code(m,a,n,_):
  if a==target:hit.append(a);m.emu_stop();return
  assert row['entry']<=a<row['end'],hex(a);steps.append(a)
 u.hook_add(unicorn.UC_HOOK_CODE,code);fn=getattr(lib,'enter_'+row['name']);fn.argtypes=[ctypes.c_void_p]
 for sample in range(256):
  data=rng.getrandbits(65536*8).to_bytes(65536,'little');u.mem_write(0x200000,data);ctypes.memmove(base+0x200000,data,len(data))
  pointer=0x205000+sample;u.mem_write(0x101d6828,struct.pack('<I',pointer));ctypes.memmove(base+0x101d6828,struct.pack('<I',pointer),4)
  state=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);values=[rng.getrandbits(32) for _ in range(8)]+[0x202|sum(bit for j,bit in enumerate([1,4,16,64,128,2048,1024]) if sample&(1<<j))];values[4]=0x204000+sample;values[5]=0x207f00+sample
  struct.pack_into('<9I',state,0,*values);struct.pack_into('<Q',state,112,base);before=state.raw
  for reg,v in zip(regs,values):u.reg_write(reg,v)
  steps.clear();hit.clear();u.emu_start(row['entry'],0,count=64);assert hit==[target]
  expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);observed.value=0;fn(state)
  actual=bytearray(state.raw)
  if row['entry']==0x5bab21:
   # IMUL leaves PF/AF/ZF/SF architecturally undefined. Compare all defined flags.
   for b in [actual,expected]:struct.pack_into('<I',b,32,struct.unpack_from('<I',b,32)[0]&~0xd4)
  assert observed.value==target and actual==expected,(row['name'],sample,'CPU/target')
  assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536)),(row['name'],sample,'stack')
  visited[row['name']]=steps.copy();cases+=1
report=dict(passed=True,script_sha256=sha(pathlib.Path(__file__).resolve()),definitions=2,cases=cases,visited_targets=targets,instructions=visited,cpu_bytes=440,preparation_sha256=sha(out/'preparation.json'),sources_sha256={str(pathlib.Path(__file__).resolve().relative_to(R)):sha(pathlib.Path(__file__).resolve())},outputs_sha256={n:sha(out/n) for n in ['isolated.ll','bridge.ll','test.dylib']},scope='512 whole-block native/patched-x86 cases:440 CPU bytes,64KiB stack,varied pointer alignment and flags. Only architecturally undefined IMUL PF/AF/ZF/SF masked in first block; all flags preserved in second. No gameplay acceptance.')
(out/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'dimension-source whole-block native/x86 cases')
