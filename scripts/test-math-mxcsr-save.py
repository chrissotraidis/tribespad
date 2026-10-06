#!/usr/bin/env python3
"""Reached STMXCSR: emitted checked ARM64 store against independent IA-32."""
import ctypes,hashlib,json,pathlib,random,struct,subprocess,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_MXCSR
from checked_support import compile_support
from guard_checked_link import guard_link
from patch_runtime_inputs import translate_math_mxcsr_save
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/math-mxcsr-save';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if len(sys.argv)==1:
 image=R/'ref/qol/IFC22.dll';needle='call void @halopad_trap_unimplemented(i32 269537011) nounwind'
 fixture='%_cpu = type opaque\ndefine void @test_math_save(%_cpu* %cpu) {\n '+needle+'\n ret void\n}\n'
 source=out/'fixture.ll';source.write_text(translate_math_mxcsr_save(fixture,image))
 for bad in [fixture.replace(needle,''),fixture+needle,fixture+'\n; @tb_math_stmxcsr_1010cef3']:
  try:translate_math_mxcsr_save(bad,image)
  except AssertionError:pass
  else:raise AssertionError('accepted changed instruction site')
 changed=out/'changed.dll';data=bytearray(image.read_bytes());data[-1]^=1;changed.write_bytes(data)
 try:
  try:translate_math_mxcsr_save(fixture,changed)
  except AssertionError:pass
  else:raise AssertionError('accepted changed image')
 finally:changed.unlink()
 math=sse_inputs('macos');sources=[R/'port/llasm-support/tribes_sse.c',source]+list(map(pathlib.Path,math['sources']))
 objects,_=compile_support(sources,out/'checked',['-DPTROFS_64BIT',*math['compile_flags']])
 meta=json.loads((out/'checked/result.json').read_text());entries=[]
 for obj in objects:
  p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
 objects,_=guard_link(entries,out/'guarded')
 sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 subprocess.run(['clang','-isysroot',sdk,'-dynamiclib',*objects,*math['libraries'],str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c'),'-o',str(out/'checked.dylib')],check=True)
lib=ctypes.CDLL(str(out/'checked.dylib'));fn=lib.test_math_save;fn.argtypes=[ctypes.c_void_p]
region=ctypes.create_string_buffer(3*4096);ptr=(ctypes.addressof(region)+4095)&~4095
lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(ptr,8192)
lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(ptr,8192);lib.tribes_memory_external_enable()
lib.tribes_memory_permissions_set.argtypes=[ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32]
cpu=ctypes.create_string_buffer(440);rng=random.Random(25034)
if len(sys.argv)>1:
 case=sys.argv[1];target=4094 if case=='cross-page' else 1
 assert lib.tribes_memory_permissions_set(0,4096,0 if case=='noaccess' else 1 if case=='readonly' else 3)
 if case=='cross-page':assert lib.tribes_memory_permissions_set(4096,4096,0)
 struct.pack_into('<I',cpu,16,(target-4)&0xffffffff);struct.pack_into('<Q',cpu,112,ptr);struct.pack_into('<I',cpu,304,0x1fa1);fn(cpu)
 raise AssertionError('accepted protected store')
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0,8192);u.mem_map(0x10000,4096);u.mem_write(0x10000,bytes.fromhex('0fae5c2404'))
captured=R/'generated/integration-review/explicit-mipmap/execution/next-instruction/cpu.bin';actual=captured.read_bytes();assert len(actual)==440 and struct.unpack_from('<I',actual,304)[0]==0x1fa1
controls=[0,1,0x1fa1,0x1f80,0x3f80,0x5f80,0x7f80,0x9fc0,0xffff]+[rng.getrandbits(16) for _ in range(64)];cases=0
for control in controls:
 for target in [0,1,2,3,7,4092,4093,4094]:
  data=rng.getrandbits(8192*8).to_bytes(8192,'little');ctypes.memmove(ptr,data,8192);u.mem_write(0,data)
  ctypes.memmove(cpu,actual if control==0x1fa1 else rng.getrandbits(440*8).to_bytes(440,'little'),440)
  struct.pack_into('<I',cpu,16,(target-4)&0xffffffff);struct.pack_into('<Q',cpu,112,ptr);struct.pack_into('<I',cpu,304,control);before=cpu.raw
  u.reg_write(UC_X86_REG_ESP,(target-4)&0xffffffff);u.reg_write(UC_X86_REG_MXCSR,control);u.emu_start(0x10000,0x10005,count=1);fn(cpu)
  assert ctypes.string_at(ptr,8192)==bytes(u.mem_read(0,8192));assert cpu.raw==before;assert u.reg_read(UC_X86_REG_MXCSR)==control;cases+=1
# Reserved high bits are not loadable on x86; the defined store clears them
# without mutating the guest CPU field, even in an intentionally poisoned fixture.
for value in [0xffffffff,0x80001fa1,0x12341f80]:
 struct.pack_into('<I',cpu,16,5);struct.pack_into('<I',cpu,304,value);before=cpu.raw;fn(cpu)
 assert struct.unpack('<I',ctypes.string_at(ptr+9,4))[0]==value&0xffff and cpu.raw==before
faults=[]
for case in ['readonly','noaccess','cross-page']:
 run=subprocess.run([sys.executable,__file__,case],capture_output=True,text=True,timeout=10)
 assert run.returncode==-6 and 'checked guest memory violation' in run.stderr,(case,run.returncode,run.stderr)
 (out/(case+'.log')).write_text(run.stderr);faults.append(case)
report=dict(passed=True,cases=cases,source_rejections=4,reserved_cases=3,checked_faults=faults,library_sha256=sha(out/'checked.dylib'),captured_cpu_sha256=sha(captured),sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'port/llasm-support/tribes_sse.c',R/'port/llasm-support/tribes_sse.h',R/'scripts/patch_runtime_inputs.py',R/'ref/qol/IFC22.dll']},scope='Source-bound original STMXCSR [ESP+4] at0x1010cef3 through emitted checked ARM64 ABI versus independent IA-32. Captured control, wrapped effective addresses, unaligned and cross-page stores, exact canaries, all CPU/XMM state preserved. Reserved high bits and checked protected-store diagnostics. Component only; no math routine or game acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'checked ARM64/IA-32 STMXCSR cases, captured control, reserved bits and three memory faults')
