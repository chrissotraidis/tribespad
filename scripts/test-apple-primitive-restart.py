#!/usr/bin/env python3
"""Check the original capability branch, preserved CPU, and finite slot resolution."""
import ctypes, hashlib, json, os, pathlib, random, struct, subprocess, sys
from apple_primitive_restart import translate, BODY_SHA
from console_fade_inputs import definition
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/apple-primitive-restart';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=(R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll').read_text()
parts=[next(s for s in source.splitlines() if s.startswith('%_cpu = type')),'''@route = global i32 0
@provider = global i32 0
@calls = global i32 0
@name = global i32 0
define i32 @tribes_graphics_get_proc(%_cpu* %cpu, i32 %n) {
 store i32 %n, i32* @name
 %c = load i32, i32* @calls
 %next = add i32 %c, 1
 store i32 %next, i32* @calls
 %result = load i32, i32* @provider
 ret i32 %result
}''']
for prefix,text in [('original',source),('apple',translate(source))]:
 body=definition(text,'loc_1001B445')
 for n in ['loc_1001B445','loc_1001B44E','loc_1001B4C9']:body=body.replace('@'+n+'(','@'+prefix+'_'+n+'(')
 parts += [body,f'''define hidden fastcc void @{prefix}_loc_1001B44E(%_cpu* %cpu) {{store i32 1, i32* @route
 ret void}}
 define hidden fastcc void @{prefix}_loc_1001B4C9(%_cpu* %cpu) {{store i32 2, i32* @route
 ret void}}
 define void @{prefix}(%_cpu* %cpu) {{call fastcc void @{prefix}_loc_1001B445(%_cpu* %cpu)
 ret void}}''']
ir=out/'blocks.ll';ir.write_text('\n'.join(parts)+'\n')
barriers=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
if len(sys.argv)==1:
 objects,_=compile_support([ir],out/'checked',[]);meta=json.loads((out/'checked/result.json').read_text())
 entries=[dict(object=p,object_sha256=sha(pathlib.Path(p)),bitcode=str(pathlib.Path(p).with_suffix('.bc')),bitcode_sha256=meta['objects'][pathlib.Path(p).stem]['bitcode_sha256']) for p in objects]
 guarded,_=guard_link(entries,out/'guarded')
 sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
 for mode,inputs in [('normal',[ir]),('checked',guarded)]:
  subprocess.run(['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-Wno-override-module','-dynamiclib',*map(str,[*barriers,*inputs]),'-o',str(out/(mode+'.dylib'))],check=True)

def exercise(mode,fault=None):
 lib=ctypes.CDLL(str(out/(mode+'.dylib')));native=ctypes.CDLL(None)
 native.mmap.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_long];native.mmap.restype=ctypes.c_void_p
 native.mprotect.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int];native.munmap.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
 size=0x100000000;base=native.mmap(None,size,0,0x1002,-1,0);assert base and base!=ctypes.c_void_p(-1).value
 page=os.sysconf('SC_PAGE_SIZE');start=0x1046c880//page*page;assert native.mprotect(base+start,page,3)==0
 lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(base,size)
 lib.tribes_memory_permissions_set.argtypes=[ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32]
 assert lib.tribes_memory_permissions_set(0,size,0);assert lib.tribes_memory_permissions_set(start,page,3)
 for f in [lib.original,lib.apple]:f.argtypes=[ctypes.c_void_p]
 lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(base,size)
 targets=(ctypes.c_void_p*2)(ctypes.cast(lib.original,ctypes.c_void_p).value,ctypes.cast(lib.apple,ctypes.c_void_p).value)
 lib.tribes_memory_register_targets.argtypes=[ctypes.POINTER(ctypes.c_void_p),ctypes.c_uint64];lib.tribes_memory_register_targets(targets,2);lib.tribes_memory_external_enable()
 globals={n:ctypes.c_uint32.in_dll(lib,n) for n in ['route','provider','calls','name']};rng=random.Random(25034);cases=0
 def store(a,v):ctypes.memmove(base+a,struct.pack('<I',v),4)
 def read(a):return struct.unpack('<I',ctypes.string_at(base+a,4))[0]
 for flag in [0,1,0x80000000,0xffffffff]:
  for slot in [0,0xfffa0800,0x12345678]:
   for provider in [0,0xfffa0800]:
    for repeat in range(16):
     seed=bytearray(rng.getrandbits(8) for _ in range(440));struct.pack_into('<Q',seed,112,base)
     store(0x1046c880,flag);store(0x1046cb08,slot)
     globals['provider'].value=provider
     if fault:
      store(0x1046c880,0);store(0x1046cb08,0);globals['provider'].value=0xfffa0800
      assert lib.tribes_memory_permissions_set(0x1046c000,4096,0 if fault=='read' else 1)
      lib.apple(ctypes.create_string_buffer(bytes(seed),440));raise AssertionError('guard returned')
     original=ctypes.create_string_buffer(bytes(seed),440);globals['route'].value=0;lib.original(original);route=globals['route'].value
     assert read(0x1046cb08)==slot and read(0x1046c880)==flag
     globals['calls'].value=globals['name'].value=globals['route'].value=0
     apple=ctypes.create_string_buffer(bytes(seed),440);lib.apple(apple)
     assert apple.raw==original.raw and globals['route'].value==route
     resolve=flag==0 and slot==0
     assert globals['calls'].value==int(resolve)
     assert globals['name'].value==(0x101bd704 if resolve else 0)
     assert read(0x1046cb08)==(provider if resolve else slot) and read(0x1046c880)==flag
     # No write is needed if lookup fails or a slot is already populated.
     if not flag and (slot or not provider):
      assert lib.tribes_memory_permissions_set(0x1046c000,4096,1)
      lib.apple(ctypes.create_string_buffer(bytes(seed),440))
      assert lib.tribes_memory_permissions_set(0x1046c000,4096,3)
     cases+=1
 assert native.munmap(base,size)==0
 return cases
if len(sys.argv)>1:exercise('checked',sys.argv[1]);raise SystemExit
report={}
for mode in ['normal','checked']:
 cases=exercise(mode);report[mode]=dict(cases=cases,binary_sha256=sha(out/(mode+'.dylib')));print('PASS',mode,cases,'original CPU/route/lookup comparisons',flush=True)
for fault in ['read','write']:
 run=subprocess.run([sys.executable,__file__,fault],capture_output=True,text=True,timeout=30);(out/(fault+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==-6 and 'memory violation' in run.stderr,(fault,run.stderr)
report.update(passed=True,checked_faults=2,original_blocks_sha256=BODY_SHA,sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/apple_primitive_restart.py',*barriers]},scope='Original GL3.1 branch and full CPU preservation; only missing restart slot uses actual resolver result. Provider stub isolates branch behavior; native provider/pixels covered by existing primitive-restart graphics tests.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
