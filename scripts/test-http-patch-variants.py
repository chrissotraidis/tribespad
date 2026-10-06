#!/usr/bin/env python3
"""Compare complete strict HTTP patch blocks with captured x86, before activation."""
import ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import capstone,unicorn
from capstone import x86_const as C
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/http-patched-engine/variants';meta=json.loads((out/'result.json').read_text());assert meta['state']=='compiled_unpublished' and not meta['published'] and meta['complete_phase'];assert sha(out/'module.ll')==meta['module_sha256']
(out/'test-result.json').write_text('{\"passed\":false,\"state\":\"running\"}\n')
parent=json.loads((out.parent/'result.json').read_text());assert sha(out.parent/'result.json')==meta['preparation_sha256'];tp=R/parent['trace_path'];assert sha(tp)==meta['trace_sha256'];trace=json.loads(tp.read_text());cp=R/'generated/integration-review/curl-version-checkpoint-20261002/native/result.json';assert sha(cp)==trace['checkpoint_sha256'];capture=json.loads(cp.read_text())
entries=sorted(meta['function_sha256']);source=(out/'module.ll').read_text()
# Rename definitions only, retaining the exact original block bodies. Every
# outgoing engine/DLL block is an observer, including other selected blocks.
for name in entries:
 pattern=r'(^define (?:hidden|protected) fastcc void @)'+name+r'\(';source,count=re.subn(pattern,lambda m:m[1]+'candidate_'+name+'(',source,flags=re.M);assert count==1,name
refs=set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(',source))-{'candidate_'+n for n in entries}
# Ignore unused declarations in the module preamble when collecting callees.
bodies=source[source.index('define '):];refs=set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(',bodies))-{'candidate_'+n for n in entries}
imports=json.loads((out.parent/'va/result.json').read_text())['imports'];targets={}
for name in refs:
 if name.startswith('loc_'):targets[name]=int(name[4:],16)
 elif name.startswith('tbimp_'):targets[name]=next(i['guest_va'] for i in imports if i['symbol']==name)
helpers={'tribes_dispatch','tribes_set_dispatch_target'};assert refs-set(targets)<=helpers,refs-set(targets)
declared=set(re.findall(r'^declare.*@([A-Za-z_0-9]+)\(',source,re.M))
source+='\n'+''.join('declare hidden fastcc void @%s(%%_cpu*)\n'%n for n in sorted(set(targets)-declared))
(out/'isolated.ll').write_text(source)
bridge=['@observed_target = global i32 0','@observed_dispatch = global i32 0']
for name,address in targets.items():bridge.append('define hidden fastcc void @%s(ptr %%cpu) { store i32 %d, ptr @observed_target\n ret void\n}'%(name,address))
bridge+=['define void @tribes_set_dispatch_target(ptr %cpu, i32 %target) { %p = getelementptr i8, ptr %cpu, i64 172\n store i32 %target, ptr %p\n ret void\n}', 'define hidden fastcc void @tribes_dispatch(ptr %cpu) { %p = getelementptr i8, ptr %cpu, i64 172\n %target = load i32, ptr %p\n store i32 %target, ptr @observed_target\n store i32 1, ptr @observed_dispatch\n ret void\n}']
for i,name in enumerate(entries):bridge+=['declare fastcc void @candidate_%s(ptr)'%name,'define void @enter_%d(ptr %%cpu) { call fastcc void @candidate_%s(ptr %%cpu)\n ret void\n}'%(i,name)]
bridgepath=out/'test-bridge.ll';bridgepath.write_text('\n'.join(bridge)+'\n');libpath=out/'test.dylib'
support=[]
# These observed blocks use integer/control-flow operations only. Do not
# link unrelated floating-point libraries into this comparison.
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(out/'isolated.ll'),str(bridgepath),*map(str,support),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));observed=ctypes.c_uint32.in_dll(lib,'observed_target');dispatched=ctypes.c_uint32.in_dll(lib,'observed_dispatch');memory=ctypes.create_string_buffer(0xb00000);base=ctypes.addressof(memory);u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
inputs=capture['http_patch_checkpoint'];pages=json.loads((cp.parent/'http-patch-pages.json').read_text())['ranges'];engine_pages=[]
for row in pages:
 if not 0x400000<=row['address']<0xa00000:continue
 item=inputs[row['file']];path=cp.parent/row['file'];assert sha(path)==item['sha256'];data=path.read_bytes();u.mem_map(row['address'],row['size']);u.mem_write(row['address'],data);ctypes.memmove(base+row['address'],data,len(data));engine_pages.append((row['address'],data))
for e in trace['code_writes']+trace['image_data_writes']:
 if e['image']=='engine':
  before=bytes(u.mem_read(e['address'],e['size']));assert before.hex()==e['before'];after=bytes.fromhex(e['after']);u.mem_write(e['address'],after);ctypes.memmove(base+e['address'],after,len(after))
# Save exact final engine data, and reset it for each case.
engine_pages=[(address,bytes(u.mem_read(address,len(data)))) for address,data in engine_pages]
u.mem_map(0x200000,65536);u.mem_map(0xfff00000,0x100000);u.mem_write(0xfff00000,b'\xcc'*0x100000);u.mem_map(0x10000000,0x200000);u.mem_write(0x10000000,b'\xcc'*0x200000)
return_target=0xfff80000;boundaries=set(targets.values())|({return_target} if "tribes_dispatch" in refs else set());expected_target=[0];entry_pc=[0];steps=[0];undefined=[0];decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True;effects={}
flag_defs=[(bit,getattr(C,'X86_EFLAGS_UNDEFINED_'+name,0),sum(getattr(C,'X86_EFLAGS_'+action+'_'+name,0) for action in ['MODIFY','RESET','SET'])) for bit,name in [(1,'CF'),(4,'PF'),(16,'AF'),(64,'ZF'),(128,'SF'),(2048,'OF')]]
def code(machine,address,size,_):
 if steps[0] and address in boundaries:expected_target[0]=address;machine.emu_stop();return
 steps[0]+=1
 if address not in effects:
  ins=next(decoder.disasm(bytes(machine.mem_read(address,size)),address));effects[address]=ins.eflags
 effect=effects[address]
 for bit,undef,defined in flag_defs:
  if effect&undef:undefined[0]|=bit
  elif effect&defined:undefined[0]&=~bit

u.hook_add(unicorn.UC_HOOK_CODE,code)
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];rng=random.Random(25034);cases=0;visited=set();per_entry={}
for i,name in enumerate(entries):
 fn=getattr(lib,'enter_%d'%i);fn.argtypes=[ctypes.c_void_p];seen=set()
 for sample in range(64):
  for address,data in engine_pages:u.mem_write(address,data);ctypes.memmove(base+address,data,len(data))
  pattern=bytearray(b''.join(struct.pack('<I',0x20b000+4*rng.randrange(3072)) for _ in range(16384)))
  # First-level object fields point to a separate method-table region. Its
  # entries transfer to the observer without executing arbitrary fixture data.
  pattern[0xb000:0xe000]=struct.pack('<I',return_target)*(0x3000//4)
  values=[0x202000+0x800*j+(sample%16)*4 for j in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884,0x400])];values[4]=0x208000+(sample%16)*4;values[5]=0x208800
  struct.pack_into('<I',pattern,values[4]-0x200000,return_target);struct.pack_into('<I',pattern,values[5]-0x200000+4,return_target)
  u.mem_write(0x200000,bytes(pattern));ctypes.memmove(base+0x200000,bytes(pattern),65536)
  cpu=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base)
  top=sample%8;cw=0x37f;tags=0xffff&~(3<<(2*top));significand=0x8000000000000000;exponent=(0x3fff+((sample//8)%2))|(0x8000 if sample&16 else 0)
  struct.pack_into('<3I',cpu,36,top,0,cw);struct.pack_into('<II',cpu,144,tags,0);struct.pack_into('<I',cpu,168,1)
  for j in range(8):
   struct.pack_into('<Q',cpu,48+8*j,significand);struct.pack_into('<H',cpu,152+2*j,exponent);u.reg_write(getattr(X,'UC_X86_REG_FP%d'%j),(significand,exponent))
  u.reg_write(X.UC_X86_REG_FPCW,cw);u.reg_write(X.UC_X86_REG_FPSW,top<<11);u.reg_write(X.UC_X86_REG_FPTAG,tags);before=cpu.raw
  for reg,value in zip(regs,values):u.reg_write(reg,value)
  expected_target[0]=0;steps[0]=0;undefined[0]=0
  try:u.emu_start(int(name[4:],16),0,count=512)
  except unicorn.UcError as error:raise AssertionError((name,sample,hex(u.reg_read(X.UC_X86_REG_EIP)),str(error)))
  assert expected_target[0] in boundaries,(name,sample,'missing boundary',hex(u.reg_read(X.UC_X86_REG_EIP)))
  expected=[u.reg_read(reg) for reg in regs];observed.value=0;dispatched.value=0;fn(cpu);assert observed.value==expected_target[0],(name,sample,'target',hex(observed.value),hex(expected_target[0]));visited.add(observed.value);seen.add(observed.value)
  actual=list(struct.unpack_from('<9I',cpu));actual[8]&=~undefined[0];expected[8]&=~undefined[0];assert actual==expected,(name,sample,actual,expected,hex(undefined[0]))
  after=bytearray(before)
  sw=u.reg_read(X.UC_X86_REG_FPSW);struct.pack_into('<3I',after,36,(sw>>11)&7,sw&~0x3800,u.reg_read(X.UC_X86_REG_FPCW));struct.pack_into('<I',after,144,u.reg_read(X.UC_X86_REG_FPTAG))
  for j in range(8):
   mantissa,exponent_after=u.reg_read(getattr(X,'UC_X86_REG_FP%d'%j));struct.pack_into('<Q',after,48+8*j,mantissa);struct.pack_into('<H',after,152+2*j,exponent_after)
  if dispatched.value:struct.pack_into('<I',after,172,observed.value)
  assert cpu.raw[36:]==after[36:],(name,sample,'extended CPU')
  assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536)),(name,sample,'stack/objects')
  for address,data in engine_pages:assert ctypes.string_at(base+address,len(data))==bytes(u.mem_read(address,len(data))),(name,sample,'engine memory',hex(address))
  cases+=1
 per_entry[name]=sorted(seen)
assert visited==boundaries,('Unvisited outgoing targets',sorted(boundaries-visited))
report=dict(passed=True,cases=cases,definitions=len(entries),visited_targets=sorted(visited),per_entry=per_entry,unvisited_targets=sorted(boundaries-visited),variants_sha256=sha(out/'result.json'),trace_sha256=sha(tp),checkpoint_sha256=sha(cp),binary_sha256=sha(libpath),isolated_module_sha256=sha(out/'isolated.ll'),bridge_sha256=sha(bridgepath),support_sha256={str(p.relative_to(R)):sha(p) for p in support},script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Every complete selected strict block compared in isolation with final HTTP-patched x86. GPRs, architecturally defined flags, extended CPU,64KiB stack/objects and all captured engine bytes compared. Outgoing blocks are observers; all declared outgoing targets exercised, but no complete path coverage, runtime publication or complete phase claim.')
(out/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'native/x86 cases across',len(entries),'complete HTTP patch definitions,',len(visited),'transfer targets')
