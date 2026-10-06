#!/usr/bin/env python3
"""Execute exact changed engine definitions against the captured patched x86."""
import ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import pefile,unicorn,capstone
from capstone import x86_const as C
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/late-patched-engine/variants';meta=json.loads((out/'result.json').read_text());assert meta['state']=='compiled_unpublished' and not meta['removed']
assert sha(out/'module.ll')==meta['module_sha256'] and sha(out/'module.o')==meta['object_sha256']
plan_path=R/'generated/integration-review/late-patch-plan.json';assert sha(plan_path)==meta['plan_sha256'];plan=json.loads(plan_path.read_text());checkpoint=R/plan['checkpoint_path'];assert sha(checkpoint)==plan['checkpoint_sha256'];capture=json.loads(checkpoint.read_text())
entries=sorted(meta['function_sha256']);targets={n:int(n[4:],16) for n in meta['external_targets'] if n.startswith('loc_')}
imports=json.loads((R/'generated/integration-review/patched-engine/va/result.json').read_text())['imports']
for name in meta['external_targets']:
 if name.startswith('tbimp_'):targets[name]=next(i['guest_va'] for i in imports if i['symbol']==name)
assert set(meta['external_targets'])==set(targets)|{'tribes_dispatch','tribes_set_dispatch_target'}
bridge=['@observed_target = global i32 0','@observed_dispatch = global i32 0']
for name,address in targets.items():bridge.append('define hidden fastcc void @%s(ptr %%cpu) { store i32 %d, ptr @observed_target\n ret void\n}'%(name,address))
# Actual helper ABI includes the private dispatch mailbox. This observer records
# the transfer without executing unrelated downstream engine or API bodies.
bridge+=['define void @tribes_set_dispatch_target(ptr %cpu, i32 %target) { %p = getelementptr i8, ptr %cpu, i64 172\n store i32 %target, ptr %p\n ret void\n}',
'define hidden fastcc void @tribes_dispatch(ptr %cpu) { %p = getelementptr i8, ptr %cpu, i64 172\n %target = load i32, ptr %p\n store i32 %target, ptr @observed_target\n store i32 1, ptr @observed_dispatch\n ret void\n}']
for i,name in enumerate(entries):bridge+=['declare fastcc void @%s(ptr)'%name,'define void @enter_%d(ptr %%cpu) { call fastcc void @%s(ptr %%cpu)\n ret void\n}'%(i,name)]
bridge_path=out/'test-bridge.ll';bridge_path.write_text('\n'.join(bridge)+'\n');libpath=out/'test.dylib';subprocess.run(['clang','-target','arm64-apple-macos12','-O0','-Wno-override-module','-dynamiclib',str(out/'module.o'),str(bridge_path),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));observed=ctypes.c_uint32.in_dll(lib,'observed_target');dispatched=ctypes.c_uint32.in_dll(lib,'observed_dispatch')
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);memory=ctypes.create_string_buffer(0xb00000);base=ctypes.addressof(memory)
# Restore actual checkpoint engine bytes (including IAT), then apply verified
# late patches; no synthetic candidate PE is mapped as a guest image.
for name,row in capture['late_patch_checkpoint'].items():
 if not name.startswith('late-page-'):continue
 match=re.fullmatch('late-page-([0-9a-f]+)-([0-9a-f]+).bin',name);address,size=[int(v,16) for v in match.groups()]
 path=R/row['path'];assert sha(path)==row['sha256']
 if 0x400000<=address<0xa00000:
  data=path.read_bytes();assert address+size<=ctypes.sizeof(memory);u.mem_map(address,size);u.mem_write(address,data);ctypes.memmove(base+address,data,size)
for row in plan['engine_regions']:
 before=bytes(u.mem_read(row['address'],row['size']));assert before.hex()==row['before_hex'];u.mem_write(row['address'],bytes.fromhex(row['after_hex']))
u.mem_map(0x200000,65536);u.mem_map(0xfff00000,0x100000);u.mem_map(0x10000000,0x200000)
return_target=0xfff80000;boundaries=set(targets.values())|{return_target};expected_target=[0];undefined_af=[False];decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True;flag_effects={}
def code(uc,address,size,_):
 if address in boundaries:expected_target[0]=address;uc.emu_stop();return
 if address not in flag_effects:
  ins=next(decoder.disasm(bytes(uc.mem_read(address,size)),address));flag_effects[address]=ins.eflags
 effect=flag_effects[address]
 if effect&C.X86_EFLAGS_UNDEFINED_AF:undefined_af[0]=True
 elif effect&(C.X86_EFLAGS_MODIFY_AF|C.X86_EFLAGS_RESET_AF|C.X86_EFLAGS_SET_AF):undefined_af[0]=False
u.hook_add(unicorn.UC_HOOK_CODE,code)
register_names=['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS'];registers=[getattr(X,'UC_X86_REG_'+n) for n in register_names]
rng=random.Random(25034);cases=0;visited=set()
for i,name in enumerate(entries):
 fn=getattr(lib,'enter_%d'%i);fn.argtypes=[ctypes.c_void_p]
 for sample in range(256):
  values=[rng.getrandbits(32) for _ in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884,0x400])];values[4]=0x208000+(sample%16)*4;values[5]=0x208800
  pattern=bytearray(struct.pack('<I',return_target)*(65536//4));struct.pack_into('<I',pattern,values[5]-0x200000-8,rng.getrandbits(32))
  u.mem_write(0x200000,bytes(pattern));ctypes.memmove(base+0x200000,bytes(pattern),65536)
  pointer=rng.getrandbits(32);u.mem_write(0x9e5ebc,struct.pack('<I',pointer));struct.pack_into('<I',memory,0x9e5ebc,pointer)
  cpu=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);before=cpu.raw
  for reg,value in zip(registers,values):u.reg_write(reg,value)
  expected_target[0]=0;undefined_af[0]=False;u.emu_start(int(name[4:],16),0,count=128);assert expected_target[0] in boundaries,(name,sample,'no downstream boundary')
  expected=[u.reg_read(reg) for reg in registers];observed.value=0;dispatched.value=0;fn(cpu)
  assert observed.value==expected_target[0],(name,sample,'target',hex(observed.value),hex(expected_target[0]));visited.add(observed.value)
  actual=list(struct.unpack_from('<9I',cpu));after=bytearray(before)
  # Logical operations leave AF undefined. Every other compared flag remains
  # architectural across these integer-only changed regions.
  if undefined_af[0]:actual[8]&=~0x10;expected[8]&=~0x10
  assert actual==expected,(name,sample,actual,expected)
  if dispatched.value:struct.pack_into('<I',after,172,observed.value)
  assert cpu.raw[36:]==after[36:],(name,sample,'non-GPR state')
  assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536)),(name,sample,'stack')
  assert struct.unpack_from('<I',memory,0x9e5ebc)[0]==pointer
  cases+=1
assert visited==boundaries,('Unvisited boundaries',sorted(boundaries-visited))
report=dict(passed=True,cases=cases,entries=entries,visited_targets=sorted(visited),variants_sha256=sha(out/'result.json'),checkpoint_sha256=sha(checkpoint),plan_sha256=sha(plan_path),binary_sha256=sha(libpath),bridge_sha256=sha(bridge_path),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='All27 exact changed LLVM definitions execute against captured patched x86 to every external transfer boundary. GPRs, defined flags excluding logical AF, complete64KiB stack and unrelated CPU state compared; private dispatch mailbox checked separately. Downstream callees are observers; no staged native activation or gameplay claim.')
(out/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'native/x86 cases,',len(entries),'changed definitions,',len(visited),'boundaries')
