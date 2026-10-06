#!/usr/bin/env python3
"""Compare the complete precompiled NOP entries to independent patched x86."""
import ctypes,hashlib,json,mmap,pathlib,random,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/input-nop';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();meta=json.loads((out/'preparation.json').read_text())
for key,root in [('inputs_sha256',R),('outputs_sha256',out)]:
 for n,h in meta[key].items():assert sha(root/n)==h,n
source=(out/'engine.ll').read_text();bridge='@observed_target = global i32 0\n'
for target in sorted({r['target'] for r in meta['records']}):bridge+='define hidden fastcc void @loc_%X(ptr %%cpu) { store i32 %d, ptr @observed_target\n ret void\n}\n'%(target,target)
for row in meta['records']:
 name=row['name'];source=source.replace('@'+name+'(','@candidate_'+name+'(');bridge+='declare hidden fastcc void @candidate_%s(ptr)\ndefine void @enter_%s(ptr %%cpu) { call fastcc void @candidate_%s(ptr %%cpu)\n ret void\n}\n'%(name,name,name)
(out/'isolated.ll').write_text(source);(out/'bridge.ll').write_text(bridge);libpath=out/'test.dylib';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(out/'isolated.ll'),str(out/'bridge.ll'),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));observed=ctypes.c_uint32.in_dll(lib,'observed_target');memory=mmap.mmap(-1,0x600000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];rng=random.Random(0x10055719);cases=0
for row in meta['records']:
 target=row['target'];u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
 for page in sorted({row['entry']&~4095,target&~4095}):u.mem_map(page,4096)
 u.mem_map(0x200000,65536);u.mem_write(target,b'\xcc');u.mem_write(row['entry'],bytes.fromhex(row['after_bytes']));hit=[]
 def code(m,a,n,_):
  if a==target:hit.append(a);m.emu_stop();return
  assert row['entry']<=a<row['end'],hex(a)
 u.hook_add(unicorn.UC_HOOK_CODE,code);fn=getattr(lib,'enter_'+row['name']);fn.argtypes=[ctypes.c_void_p]
 for sample in range(128):
  data=rng.getrandbits(65536*8).to_bytes(65536,'little');u.mem_write(0x200000,data);ctypes.memmove(base+0x200000,data,len(data));state=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);values=[rng.getrandbits(32) for _ in range(8)]+[0x202|sum(bit for j,bit in enumerate([1,4,16,64,128,2048,1024]) if sample&(1<<j))];values[4]=0x204000+sample*4;struct.pack_into('<9I',state,0,*values);struct.pack_into('<Q',state,112,base);before=state.raw
  for reg,v in zip(regs,values):u.reg_write(reg,v)
  hit.clear();u.emu_start(row['entry'],0,count=16);assert hit==[target];expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);observed.value=0;fn(state)
  assert observed.value==target and state.raw==expected,(row['name'],sample,'CPU/target');assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536)),(row['name'],sample,'stack');cases+=1
report=dict(passed=True,published=False,cases=cases,definitions=2,preparation_sha256=sha(out/'preparation.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Whole NOP-entry native/x86 comparisons, all440 CPU bytes and64KiB stack. Callees are observation endpoints, not API emulation or game acceptance.')
(out/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'native/x86 NOP comparisons')
