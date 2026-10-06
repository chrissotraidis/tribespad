#!/usr/bin/env python3
"""Reached memory BTS/BT sequence: emitted checked ARM64 against independent x86."""
import ctypes,hashlib,json,pathlib,random,struct,subprocess,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS
from checked_support import compile_support
from guard_checked_link import guard_link
from patch_runtime_inputs import translate_startup_memory_bits
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/startup-memory-bits';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if len(sys.argv)==1:
 image=R/'ref/qol/IFC22.dll';needles=['call void @halopad_trap_unimplemented(i32 %d) nounwind'%pc for pc in [0x10105216,0x1010523b,0x10105269]];needle=needles[0]
 fixture='%_cpu = type opaque\n'+''.join('define void @test_startup_bits%d(%%_cpu* %%cpu) {\n '%i+n+'\n ret void\n}\n' for i,n in enumerate(needles));source=out/'fixture.ll';source.write_text(translate_startup_memory_bits(fixture,image))
 for bad in [fixture.replace(needle,''),fixture+needle,fixture+'\n; @tb_startup_bts_10105216']:
  try:translate_startup_memory_bits(bad,image)
  except AssertionError:pass
  else:raise AssertionError('accepted changed instruction site')
 changed=out/'changed.dll';data=bytearray(image.read_bytes());data[-1]^=1;changed.write_bytes(data)
 try:
  try:translate_startup_memory_bits(fixture,changed)
  except AssertionError:pass
  else:raise AssertionError('accepted changed image')
 finally:changed.unlink()
 objects,_=compile_support([R/'port/llasm-support/tribes_scan.c',source],out/'checked',['-DPTROFS_64BIT'])
 meta=json.loads((out/'checked/result.json').read_text());entries=[]
 for obj in objects:
  p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
 objects,_=guard_link(entries,out/'guarded')
 sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 subprocess.run(['clang','-isysroot',sdk,'-dynamiclib',*objects,str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c'),'-o',str(out/'checked.dylib')],check=True)
lib=ctypes.CDLL(str(out/'checked.dylib'));functions=[getattr(lib,'test_startup_bits'+str(i)) for i in range(3)]
for fn in functions:fn.argtypes=[ctypes.c_void_p]
region=ctypes.create_string_buffer(3*4096);ptr=(ctypes.addressof(region)+4095)&~4095;base=ptr-0x10000
lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(base,0x12000)
lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(base,0x12000);lib.tribes_memory_external_enable()
lib.tribes_memory_permissions_set.argtypes=[ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32]
rng=random.Random(25034);cpu=ctypes.create_string_buffer(440)
if len(sys.argv)>1:
 case=sys.argv[1];which=int(sys.argv[2]);fn=functions[which];target=0x10ffe if case=='cross-page' else 0x10001
 permission=0 if case=='unreadable' else 1 if case=='readonly' else 3
 assert lib.tribes_memory_permissions_set(0x10000,4096,permission)
 if case=='cross-page':assert lib.tribes_memory_permissions_set(0x11000,4096,0)
 struct.pack_into('<I',cpu,0,58);struct.pack_into('<I',cpu,8 if which==2 else 24,(target-4)&0xffffffff);struct.pack_into('<Q',cpu,112,base);fn(cpu)
 if case=='readonly' and which:print('PASS: BT reads readonly bitset');sys.exit(0)
 raise AssertionError('accepted protected bitset')
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x8000,4096);u.mem_write(0x8000,bytes.fromhex('0fab06'));u.mem_map(0x10000,8192)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS]
indices=list(range(256))+[0x7fffffff,0x80000000,0xffffffff,0xffffffe0]+[rng.getrandbits(32) for _ in range(32)];cases=0
for which,fn in enumerate(functions):
 for index in indices:
  signed=index if index<0x80000000 else index-0x100000000
  for offset in [0,1,31,4092]:
   target=0x10000+offset
   for value in [0,0xffffffff,1<<(index&31),~(1<<(index&31))&0xffffffff]:
    data=bytearray(rng.getrandbits(8192*8).to_bytes(8192,'little'));struct.pack_into('<I',data,offset,value);ctypes.memmove(ptr,bytes(data),8192);u.mem_write(0x10000,bytes(data))
    values=[rng.getrandbits(32) for _ in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884])];values[0]=index;values[2 if which==2 else 6]=(target-(signed//32)*4)&0xffffffff
    ctypes.memmove(cpu,rng.getrandbits(440*8).to_bytes(440,'little'),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);before=cpu.raw
    for reg,v in zip(regs,values):u.reg_write(reg,v)
    u.mem_write(0x8000,bytes.fromhex(['0fab06','0fa306','0fa302'][which]));u.emu_start(0x8000,0x8003,count=1);expected=[u.reg_read(reg) for reg in regs];fn(cpu);actual=list(struct.unpack_from('<9I',cpu))
    assert actual[:8]==expected[:8] and (actual[8]&0x41)==(expected[8]&0x41),(index,offset,actual,expected)
    assert actual[8]&~1==values[8]&~1 and cpu.raw[36:]==before[36:]
    assert ctypes.string_at(ptr,8192)==bytes(u.mem_read(0x10000,8192));cases+=1
faults=[]
for which in range(3):
 for case in ['readonly','unreadable','cross-page']:
  run=subprocess.run([sys.executable,__file__,case,str(which)],capture_output=True,text=True,timeout=10)
  if case=='readonly' and which:assert run.returncode==0 and 'PASS:' in run.stdout;continue
  assert run.returncode==-6 and 'checked guest memory violation' in run.stderr,(which,case,run.returncode,run.stderr)
  label=str(which)+'-'+case;(out/(label+'.log')).write_text(run.stderr);faults.append(label)
report=dict(passed=True,cases=cases,source_rejections=4,checked_faults=faults,library_sha256=sha(out/'checked.dylib'),sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'port/llasm-support/tribes_scan.c',R/'scripts/patch_runtime_inputs.py',R/'ref/qol/IFC22.dll']},scope='Exact non-LOCK BTS [ESI],EAX / BT [ESI],EAX / BT [EDX],EAX at 0x10105216/0x1010523b/0x10105269; signed dword offsets and wrapped 32-bit addresses, unaligned memory, CF/ZF against x86, other flags retained by policy, all unrelated CPU and bitset bytes preserved. Checked load/store and source/image rejection. Component only.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'checked ARM64/x86 memory BTS/BT cases and',len(faults),'memory faults')
