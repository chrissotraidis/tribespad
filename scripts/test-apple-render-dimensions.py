#!/usr/bin/env python3
"""Compare actual original renderer blocks and checked default routing."""
import ctypes,hashlib,json,os,pathlib,random,struct,subprocess,sys
from apple_render_dimensions import translate,BODY_SHA
from console_fade_inputs import definition
from checked_support import compile_support
from guard_checked_link import guard_link
from sse_build_inputs import sse_inputs

R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/apple-render-dimensions';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll'
text=original.read_text();adapted=translate(text)
types=next(s for s in text.splitlines() if s.startswith('%_cpu = type'))
helpers=['tb_sse_cvttss2si','tb_sse_dq2ps','tb_sse_load','tb_sse_scalar','tb_sse_setd']
parts=[types,*[next(s for s in text.splitlines() if s.startswith('declare ') and '@'+name+'(' in s) for name in helpers]]
continuations=['loc_10043265','loc_100432F3','loc_100325B0']
entry_count=len(BODY_SHA)
for prefix,module in [('original',text),('apple',adapted)]:
 blocks=''.join(definition(module,n) for n in [*BODY_SHA,'loc_10042B9E'])
 for n in [*BODY_SHA,'loc_10042B9E',*continuations]:
  blocks=blocks.replace('@'+n+'(', '@'+prefix+'_'+n+'(')
 parts.append(blocks)
 for n in continuations:
  parts.append(f'define hidden fastcc void @{prefix}_{n}(%_cpu* %cpu) {{ ret void }}')
 for i,n in enumerate(BODY_SHA):
  parts.append(f'define void @{prefix}_{i}(%_cpu* %cpu) {{ call fastcc void @{prefix}_{n}(%_cpu* %cpu)\n ret void }}')
ir=out/'blocks.ll';ir.write_text('\n'.join(parts)+'\n')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
flags=['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-Wno-override-module']
barriers=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
math=sse_inputs('macos')
compile_flags=['-DPTROFS_64BIT',*math['compile_flags']]
support=[R/'port/llasm-support/tribes_sse.c',*map(pathlib.Path,math['sources'])]
if len(sys.argv)==1:
 objects,_=compile_support([ir,*support],out/'checked',compile_flags)
 meta=json.loads((out/'checked/result.json').read_text())
 entries=[dict(object=p,object_sha256=sha(pathlib.Path(p)),bitcode=str(pathlib.Path(p).with_suffix('.bc')),bitcode_sha256=meta['objects'][pathlib.Path(p).stem]['bitcode_sha256']) for p in objects]
 guarded,_=guard_link(entries,out/'guarded')
 for mode,inputs in [('normal',[ir,*support]),('checked',guarded)]:
  subprocess.run(flags+compile_flags+['-dynamiclib',*map(str,[*barriers,*inputs]),*math['libraries'],'-o',str(out/(mode+'.dylib'))],check=True)

def exercise(mode,fault=None):
 lib=ctypes.CDLL(str(out/(mode+'.dylib')));native=ctypes.CDLL(None)
 native.mmap.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_long];native.mmap.restype=ctypes.c_void_p
 native.mprotect.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int]
 native.munmap.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
 size=0x100000000;base=native.mmap(None,size,0,0x1002,-1,0);assert base and base!=ctypes.c_void_p(-1).value
 page_size=os.sysconf('SC_PAGE_SIZE')
 pages=sorted({p//page_size*page_size for p in [0x100000,0x200000,0x300000,0x101d6828,0x1046ce60,0x1046e258]})
 for p in pages:assert native.mprotect(base+p,page_size,3)==0
 lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(base,size)
 lib.tribes_memory_permissions_set.argtypes=[ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32]
 assert lib.tribes_memory_permissions_set(0,size,0)
 for p in pages:assert lib.tribes_memory_permissions_set(p,page_size,3)
 targets=(ctypes.c_void_p*(entry_count*2))(*[ctypes.cast(getattr(lib,f'{prefix}_{i}'),ctypes.c_void_p).value for prefix in ['original','apple'] for i in range(entry_count)])
 lib.tribes_memory_register_targets.argtypes=[ctypes.POINTER(ctypes.c_void_p),ctypes.c_uint64];lib.tribes_memory_register_targets(targets,entry_count*2)
 lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(base,size);lib.tribes_memory_external_enable()
 for prefix in ['original','apple']:
  for i in range(entry_count):getattr(lib,f'{prefix}_{i}').argtypes=[ctypes.c_void_p]
 def store(p,v):ctypes.memmove(base+p,struct.pack('<I',v),4)
 store(0x101d6828,0x300001);store(0x1046ce60,0);store(0x1046e258,0)
 store(0x101d66e8,0x3f800000);store(0x101d66f0,0x3f800000)
 for j,v in enumerate([13,29,640,480]):store(0x300100+j*4,v)
 store(0x100848,0x300100)
 cpu=ctypes.create_string_buffer(bytes(440),440);struct.pack_into('<Q',cpu,112,base);struct.pack_into('<I',cpu,12,0x200080);struct.pack_into('<I',cpu,16,0x100800)
 struct.pack_into('<I',cpu,20,0x100840);struct.pack_into('<I',cpu,304,0x1f80)
 if fault:
  entry=0
  if ':' in fault:entry_text,fault=fault.split(':',1);entry=int(entry_text);assert 0<=entry<entry_count
  target=0
  if fault=='nonzero':target=0x400000
  elif fault=='protected':
   target=0x200100;assert native.mprotect(base+0x200000,page_size,0)==0;assert lib.tribes_memory_permissions_set(0x200000,page_size,0)
  elif fault=='interop':store(0x1046ce60,1)
  elif fault=='current':store(0x1046e258,0x200100)
  elif fault=='dimensions':store(0x101d6828,0x400000)
  else:raise AssertionError(fault)
  store(0x1046ce48,target);getattr(lib,f'apple_{entry}')(cpu);raise AssertionError('fault returned')
 rng=random.Random(25034);cases=0
 for i in range(128):
  width,height=rng.randrange(1,4096),rng.randrange(1,4096);target_width,target_height=rng.randrange(1,4096),rng.randrange(1,4096)
  store(0x300001,width);store(0x300005,height)
  for j,v in enumerate([rng.randrange(-128,129),rng.randrange(-128,129),width,height]):store(0x300100+j*4,v&0xffffffff)
  store(0x101d66e8,0x3f000000 if i&1 else 0x3f800000);store(0x101d66f0,0x40000000 if i&2 else 0x3f800000)
  owner=0x7b49e4 if i%2 else rng.getrandbits(32);store(0x200080,owner)
  seed=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440)
  struct.pack_into('<Q',seed,112,base);struct.pack_into('<I',seed,12,0x200080);struct.pack_into('<I',seed,16,0x100800)
  struct.pack_into('<I',seed,20,0x100840);struct.pack_into('<I',seed,304,0x1f80|((i%4)<<13))
  stack=bytes(rng.getrandbits(8) for _ in range(512))
  for default in [False,True]:
   store(0x1046ce60,0 if default else i%3);store(0x1046e258,0 if default else (0x200100 if i%2 else 0))
   store(0x200124,width if default else target_width);store(0x200128,height if default else target_height)
   for entry in range(entry_count):
    results=[]
    for prefix in ['original','apple']:
     c=ctypes.create_string_buffer(seed.raw,440)
     if entry<2:struct.pack_into('<I',c,8,0 if default and prefix=='apple' else 0x200100)
     store(0x1046ce48,0 if default and prefix=='apple' else 0x200100)
     ctypes.memmove(base+0x1007a0,stack,512);store(0x100848,0x300100);getattr(lib,f'{prefix}_{entry}')(c)
     # The reference supplies an object with the window's real dimensions;
     # the Apple path keeps the target null. Normalize only that pointer
     # when it survives in a GPR; all CPU/flags/FP and stack otherwise match.
     if default and entry<4:struct.pack_into('<I',c,4 if entry>=2 else 8,0)
     if entry==2:
      expected=(0,0,width if default else target_width,height if default else target_height)
      assert struct.unpack('<4I',ctypes.string_at(base+0x100820,16))==expected,(mode,i,default,prefix)
     if entry==3:
      expected=(0,(height-(height if default else target_height))&0xffffffff,width if default else target_width,height if default else target_height)
      assert struct.unpack('<4I',ctypes.string_at(base+0x1007ec,16))==expected,(mode,i,default,prefix)
     results.append((c.raw,ctypes.string_at(base+0x1007a0,512)))
    assert results[0]==results[1],(mode,i,default,entry);cases+=1
 assert native.munmap(base,size)==0
 return cases

if len(sys.argv)>1:
 exercise('checked',sys.argv[1]);sys.exit(99)
report=dict(passed=False)
for mode in ['normal','checked']:
 cases=exercise(mode);report[mode]=dict(passed=True,cases=cases,binary_sha256=sha(out/(mode+'.dylib')));print('PASS:',mode,cases,'original-object and window-dimension CPU/stack comparisons',flush=True)
faults=[]
for name in [f'{entry}:{fault}' for entry in [0,2,3,4,5,6] for fault in ['nonzero','protected','interop','current','dimensions']]:
 run=subprocess.run([sys.executable,str(pathlib.Path(__file__).resolve()),name],capture_output=True,text=True,timeout=30)
 (out/(name+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==-6 and 'memory violation' in run.stderr,(name,run.returncode,run.stderr)
 faults.append(name)
report.update(passed=True,checked_faults=faults,sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/apple_render_dimensions.py',*barriers,*support,R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.h',R/'port/core/sse_arithmetic.h']},math=math['evidence'],original_blocks_sha256=BODY_SHA,scope='Seven actual renderer blocks and original viewport continuation with real SSE/SoftFloat helpers; original nonzero target behavior preserved, default dimensions compared to original object dimensions, full440 CPU/512-byte stack comparison with only surviving absent object pointers normalized, independent viewport rectangles and thirty checked invalid/protected reads. Component test; no playable-map acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
