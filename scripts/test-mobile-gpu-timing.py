#!/usr/bin/env python3
"""Execute the finite mobile timing adaptation through normal and checked code."""
import ctypes, hashlib, json, os, pathlib, random, struct, subprocess
from mobile_gpu_timing import BLOCKS, translate
from console_fade_inputs import definition
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/mobile-gpu-timing';out.mkdir(parents=True,exist_ok=True)
source=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe/qol-patched.ll'
text=source.read_text();mobile=translate(text);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
parts=[next(s for s in text.splitlines() if s.startswith('%_cpu = type')),'''@route = global i32 0
@called = global i32 0
@pop = global i32 0
define void @tribes_set_dispatch_target(%_cpu* %cpu, i32 %target) {store i32 %target, i32* @called
ret void}
define hidden fastcc void @tribes_dispatch(%_cpu* %cpu) {
 %p=getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 4
 %sp=load i32, i32* %p
 %pop=load i32, i32* @pop
 %next=add i32 %sp, %pop
 store i32 %next, i32* %p
 ret void}
''']
for i,(name,(_,continuation)) in enumerate(BLOCKS.items()):
 b=definition(mobile,name);parts += [b,f'define hidden fastcc void @{continuation}(%_cpu* %cpu) {{store i32 {i+1}, i32* @route\nret void}}',f'define void @mobile_{i}(%_cpu* %cpu) {{call fastcc void @{name}(%_cpu* %cpu)\nret void}}']
 if i<3:
  b=definition(text,name).replace('@'+name+'(', '@reference_'+name+'(')
  parts += [b,f'define void @original_{i}(%_cpu* %cpu) {{call fastcc void @reference_{name}(%_cpu* %cpu)\nret void}}']
ir=out/'blocks.ll';ir.write_text('\n'.join(parts)+'\n')
objects,_=compile_support([ir],out/'checked',[]);meta=json.loads((out/'checked/result.json').read_text())
entries=[dict(object=p,object_sha256=sha(pathlib.Path(p)),bitcode=str(pathlib.Path(p).with_suffix('.bc')),bitcode_sha256=meta['objects'][pathlib.Path(p).stem]['bitcode_sha256']) for p in objects]
guarded,_=guard_link(entries,out/'guarded');sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
barriers=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
native=ctypes.CDLL(None);native.mmap.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_long];native.mmap.restype=ctypes.c_void_p
native.mprotect.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int];native.munmap.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
results={}
for mode,inputs in [('normal',[ir]),('checked',guarded)]:
 libpath=out/(mode+'.dylib');subprocess.run(['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-Wno-override-module','-dynamiclib',*map(str,[*barriers,*inputs]),'-o',str(libpath)],check=True)
 lib=ctypes.CDLL(str(libpath));base=native.mmap(None,0x100000000,0,0x1002,-1,0);assert base and base!=ctypes.c_void_p(-1).value
 regions=[(0x200000,0x4000),(0x1046c000,0x4000)]
 for start,size in regions:assert native.mprotect(base+start,size,3)==0
 lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(base,0x100000000)
 lib.tribes_memory_permissions_set.argtypes=[ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32];assert lib.tribes_memory_permissions_set(0,0x100000000,0)
 for start,size in regions:assert lib.tribes_memory_permissions_set(start,size,3)
 lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(base,0x100000000)
 targets=(ctypes.c_void_p*4)(*[ctypes.cast(getattr(lib,'mobile_'+str(i)),ctypes.c_void_p).value for i in range(4)])
 lib.tribes_memory_register_targets.argtypes=[ctypes.POINTER(ctypes.c_void_p),ctypes.c_uint64];lib.tribes_memory_register_targets(targets,4);lib.tribes_memory_external_enable()
 route=ctypes.c_uint32.in_dll(lib,'route');called=ctypes.c_uint32.in_dll(lib,'called');pop=ctypes.c_uint32.in_dll(lib,'pop');rng=random.Random(25034)
 cases=0
 for i in range(4):
  fn=getattr(lib,'mobile_'+str(i));fn.argtypes=[ctypes.c_void_p]
  for sample in range(128):
   seed=bytearray(rng.getrandbits(8) for _ in range(440));struct.pack_into('<Q',seed,112,base);struct.pack_into('<I',seed,16,0x202000)
   snapshot=bytearray(rng.getrandbits(8) for _ in range(0x4000));snapshot[0x17a8]=sample%32
   for addr in [0x1046c76c,0x1046c84c,0x1046c808]:struct.pack_into('<I',snapshot,addr-0x1046c000,0xfeed1234)
   ctypes.memmove(base+0x1046c000,bytes(snapshot),len(snapshot));ctypes.memset(base+0x200000,0xa5,0x4000)
   expected=bytes(seed);expected_memory=bytes(snapshot)
   if i<3:
    original=getattr(lib,'original_'+str(i));original.argtypes=[ctypes.c_void_p];before=ctypes.create_string_buffer(bytes(seed),440);pop.value=8 if i==2 else 12;called.value=0;original(before)
    assert called.value==0xfeed1234
    expected=before.raw;expected_memory=ctypes.string_at(base+0x1046c000,0x4000)
    ctypes.memmove(base+0x1046c000,bytes(snapshot),len(snapshot));ctypes.memset(base+0x200000,0xa5,0x4000)
   actual=ctypes.create_string_buffer(bytes(seed),440);called.value=route.value=0;fn(actual)
   assert actual.raw==expected,(mode,i,'CPU')
   assert ctypes.string_at(base+0x1046c000,0x4000)==expected_memory,(mode,i,'data')
   assert ctypes.string_at(base+0x200000,0x4000)==b'\xa5'*0x4000,(mode,i,'stack')
   assert route.value==i+1 and called.value==0
   cases+=1
 assert native.munmap(base,0x100000000)==0
 results[mode]=dict(cases=cases,binary_sha256=sha(libpath));print('PASS',mode,cases,'CPU, stack, data, continuation and no query calls',flush=True)
# The route must reject a changed original block before compiling anything.
try:translate(text.replace(';mov tmpadr, 0x1046d858\n',';changed original\n',1))
except AssertionError:pass
else:raise AssertionError('modified original accepted')
results.update(passed=True,blocks=BLOCKS,sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/mobile_gpu_timing.py',*barriers]},scope='Four finite mobile timing routes. Original gen/begin/end blocks compared with an isolated stdcall query boundary; mobile report path leaves pre-sample estimator state intact. No rendering or scheduling-performance acceptance.')
(out/'result.json').write_text(json.dumps(results,indent=2)+'\n')
