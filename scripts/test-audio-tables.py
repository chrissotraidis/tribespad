#!/usr/bin/env python3
"""Compare integer audio-table blocks with patched x86; compile all variants."""
import ctypes,hashlib,json,mmap,pathlib,re,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/audio-tables';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
m=json.loads((P/'preparation.json').read_text())
for key,root in [('inputs_sha256',R),('outputs_sha256',P)]:
 for n,h in m[key].items():assert sha(root/n)==h,n
source=(P/'engine.ll').read_text();blocks={}
for match in re.finditer(r'^define .*?@(loc_[0-9A-F]+)\(',source,re.M):blocks[match[1]]=source[match.start():source.index('\n}',match.start())+3]
subprocess.run(['clang','-isysroot',sdk,'-target','arm64-apple-macos12','-O2','-Wno-override-module','-c',str(P/'engine.ll'),'-o',str(P/'engine-test.o')],check=True)
selected=[];deferred=[]
for row in m['records']:
 body=blocks[row['name']];calls=set(re.findall(r'call .*?@(\w+)\(',body))
 if any(not n.startswith('loc_') and n not in ('tribes_dispatch','tribes_set_dispatch_target') for n in calls):deferred.append(row['name'])
 else:selected.append(row)
assert len(selected)==113 and len(deferred)==13
refs={n for row in selected for n in re.findall(r'call .*?@(loc_[0-9A-F]+)\(',blocks[row['name']])}
prefix=source[:source.index('define ')];prefix=re.sub(r'^declare .*?@loc_[0-9A-F]+\([^\n]*\n','',prefix,flags=re.M)
bridge='@observed = global i32 0\n'
for n in refs:bridge+=f'define hidden fastcc void @{n}(%_cpu* %cpu) {{store i32 {int(n[4:],16)}, i32* @observed\n ret void\n}}\n'
bridge+='''define void @tribes_set_dispatch_target(%_cpu* %cpu, i32 %target) {
 %p = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 17
 store i32 %target, i32* %p
 ret void
}
define hidden fastcc void @tribes_dispatch(%_cpu* %cpu) {
 %p = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 17
 %t = load i32, i32* %p
 store i32 %t, i32* @observed
 ret void
}
'''
# Mailbox is byte172; use byte addressing independent of CPU field layout.
bridge=bridge.replace('%p = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 17','%b = bitcast %_cpu* %cpu to i8*\n %a = getelementptr i8, i8* %b, i64 172\n %p = bitcast i8* %a to i32*')
prefix=re.sub(r'^declare .*?@(?:tribes_dispatch|tribes_set_dispatch_target)\([^\n]*\n','',prefix,flags=re.M)
bodies=''
for row in selected:
 n=row['name'];body=blocks[n];bodies+=re.sub(r'define (?:hidden|protected) fastcc void @'+n+r'\(',f'define void @tested_{n}(',body,count=1).replace('musttail call','call')
(P/'test.ll').write_text(prefix+bridge+bodies)
libpath=P/'test.dylib';subprocess.run(['clang','-isysroot',sdk,'-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(P/'test.ll'),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));seen=ctypes.c_uint32.in_dll(lib,'observed');memory=mmap.mmap(-1,0x100000000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory))
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];cases=0;visited=set()
for row in selected:
 print('Checking',row['name'],flush=True);fn=getattr(lib,'tested_'+row['name']);fn.argtypes=[ctypes.c_void_p]
 for sample,index in enumerate([0,1,15,16,17,31,62,63,64,127,255,1023]):
  u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);pages={};hit=[];steps=[]
  def page(at):
   at &=~4095
   if at not in pages:
    data=struct.pack('<I',0x2000100+sample*16)*1024;u.mem_map(at,4096);u.mem_write(at,data);pages[at]=data
  def unmapped(c,access,a,n,value,_):
   for p in range(a&~4095,(a+n+4095)&~4095,4096):page(p)
   return True
  u.hook_add(unicorn.UC_HOOK_MEM_UNMAPPED,unmapped)
  for p in range(row['entry']&~4095,(row['end']+4095)&~4095,4096):page(p)
  u.mem_write(row['entry'],bytes.fromhex(row['after_bytes']))
  for p in list(pages):pages[p]=bytes(u.mem_read(p,4096))
  values=[0x2000100+sample*16]*8+[0x202|(sample&0xd5)];values[3]=index;values[4]=0x2100800+sample;values[5]=0x2100700+sample
  for reg,value in zip(regs,values):u.reg_write(reg,value)
  def code(c,a,n,_):
   if steps and (a in {int(t[4:],16) for t in refs} or not row['entry']<=a<row['end']):hit.append(a);c.emu_stop();return
   steps.append(a)
  u.hook_add(unicorn.UC_HOOK_CODE,code);u.emu_start(row['entry'],0,count=256);assert len(hit)==1,(row['name'],steps)
  for at,data in pages.items():ctypes.memmove(base+at,data,4096)
  state=ctypes.create_string_buffer(440);struct.pack_into('<9I',state,0,*values);struct.pack_into('<Q',state,112,base);expected=bytearray(state.raw);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs])
  if '@tribes_set_dispatch_target(' in blocks[row['name']]:struct.pack_into('<I',expected,172,hit[0])
  seen.value=0;fn(state);actual=bytearray(state.raw)
  # IMUL defines only CF/OF; source-count comparisons after it define all flags.
  assert actual==expected and seen.value==hit[0],(row['name'],sample,'CPU/target',[(i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b],hex(seen.value),hex(hit[0]))
  for at in pages:assert ctypes.string_at(base+at,4096)==bytes(u.mem_read(at,4096)),(row['name'],sample,'memory',hex(at))
  visited.update(w for w in row['writes'] if any(a<=w<a+15 for a in steps));cases+=1
report=dict(passed=True,cases=cases,whole_integer_blocks=len(selected),compiled_blocks=len(m['records']),deferred_x87_shift_blocks=deferred,preparation_sha256=sha(P/'preparation.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='113 whole integer blocks against patched x86: CPU440 bytes,flags,dispatch and touched memory,12 source indices around old/new limits. All126 variants compiled;13 x87/shift blocks not dynamically compared. No gameplay acceptance.')
(P/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',cases,'native/x86 cases')
