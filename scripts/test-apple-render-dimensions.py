#!/usr/bin/env python3
"""Compare the two actual original renderer blocks and checked default routing."""
import ctypes,hashlib,json,os,pathlib,random,struct,subprocess,sys
from apple_render_dimensions import translate,BODY_SHA
from console_fade_inputs import definition
from checked_support import compile_support
from guard_checked_link import guard_link

R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/apple-render-dimensions';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll'
text=original.read_text();adapted=translate(text)
types=next(s for s in text.splitlines() if s.startswith('%_cpu = type'))
parts=[types]
for prefix,module in [('original',text),('apple',adapted)]:
 blocks=''.join(definition(module,n) for n in BODY_SHA)
 for n in [*BODY_SHA,'loc_10043265','loc_100432F3']:
  blocks=blocks.replace('@'+n+'(', '@'+prefix+'_'+n+'(')
 parts.append(blocks)
 for n in ['loc_10043265','loc_100432F3']:
  parts.append(f'define hidden fastcc void @{prefix}_{n}(%_cpu* %cpu) {{ ret void }}')
 for i,n in enumerate(BODY_SHA):
  parts.append(f'define void @{prefix}_{i}(%_cpu* %cpu) {{ call fastcc void @{prefix}_{n}(%_cpu* %cpu)\n ret void }}')
ir=out/'blocks.ll';ir.write_text('\n'.join(parts)+'\n')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
flags=['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-Wno-override-module']
barriers=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
if len(sys.argv)==1:
 objects,_=compile_support([ir],out/'checked',[])
 meta=json.loads((out/'checked/result.json').read_text())
 entries=[dict(object=p,object_sha256=sha(pathlib.Path(p)),bitcode=str(pathlib.Path(p).with_suffix('.bc')),bitcode_sha256=meta['objects'][pathlib.Path(p).stem]['bitcode_sha256']) for p in objects]
 guarded,_=guard_link(entries,out/'guarded')
 for mode,inputs in [('normal',[ir]),('checked',guarded)]:
  subprocess.run(flags+['-dynamiclib',*map(str,[*barriers,*inputs]),'-o',str(out/(mode+'.dylib'))],check=True)

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
 targets=(ctypes.c_void_p*4)(*[ctypes.cast(getattr(lib,f'{prefix}_{i}'),ctypes.c_void_p).value for prefix in ['original','apple'] for i in range(2)])
 lib.tribes_memory_register_targets.argtypes=[ctypes.POINTER(ctypes.c_void_p),ctypes.c_uint64];lib.tribes_memory_register_targets(targets,4)
 lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(base,size);lib.tribes_memory_external_enable()
 for prefix in ['original','apple']:
  for i in range(2):getattr(lib,f'{prefix}_{i}').argtypes=[ctypes.c_void_p]
 def store(p,v):ctypes.memmove(base+p,struct.pack('<I',v),4)
 store(0x101d6828,0x300001);store(0x1046ce60,0);store(0x1046e258,0)
 cpu=ctypes.create_string_buffer(bytes(440),440);struct.pack_into('<Q',cpu,112,base);struct.pack_into('<I',cpu,12,0x200080);struct.pack_into('<I',cpu,16,0x100800)
 if fault:
  target=0
  if fault=='nonzero':target=0x400000
  elif fault=='protected':
   target=0x200100;assert native.mprotect(base+0x200000,page_size,0)==0;assert lib.tribes_memory_permissions_set(0x200000,page_size,0)
  elif fault=='interop':store(0x1046ce60,1)
  elif fault=='current':store(0x1046e258,0x200100)
  elif fault=='dimensions':store(0x101d6828,0x400000)
  else:raise AssertionError(fault)
  store(0x1046ce48,target);getattr(lib,'apple_0')(cpu);raise AssertionError('fault returned')
 rng=random.Random(25034);cases=0
 for i in range(128):
  width,height=rng.randrange(1,4096),rng.randrange(1,4096);target_width,target_height=rng.randrange(1,4096),rng.randrange(1,4096)
  store(0x300001,width);store(0x300005,height)
  owner=0x7b49e4 if i%2 else rng.getrandbits(32);store(0x200080,owner)
  seed=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440)
  struct.pack_into('<Q',seed,112,base);struct.pack_into('<I',seed,12,0x200080);struct.pack_into('<I',seed,16,0x100800)
  stack=bytes(rng.getrandbits(8) for _ in range(128))
  for default in [False,True]:
   store(0x1046ce60,0 if default else i%3);store(0x1046e258,0 if default else (0x200100 if i%2 else 0))
   store(0x200124,width if default else target_width);store(0x200128,height if default else target_height)
   for entry in range(2):
    results=[]
    for prefix in ['original','apple']:
     c=ctypes.create_string_buffer(seed.raw,440);struct.pack_into('<I',c,8,0 if default and prefix=='apple' else 0x200100)
     store(0x1046ce48,0 if default and prefix=='apple' else 0x200100)
     ctypes.memmove(base+0x1007e0,stack,128);getattr(lib,f'{prefix}_{entry}')(c)
     # The reference supplies an object with the window's real dimensions;
     # the Apple path deliberately keeps EDX/global target null. Normalize
     # that input pointer alone; all other CPU/flags/FP and stack must match.
     if default:struct.pack_into('<I',c,8,0)
     results.append((c.raw,ctypes.string_at(base+0x1007e0,128)))
    assert results[0]==results[1],(mode,i,default,entry);cases+=1
 assert native.munmap(base,size)==0
 return cases

if len(sys.argv)>1:
 exercise('checked',sys.argv[1]);sys.exit(99)
report=dict(passed=False)
for mode in ['normal','checked']:
 cases=exercise(mode);report[mode]=dict(passed=True,cases=cases,binary_sha256=sha(out/(mode+'.dylib')));print('PASS:',mode,cases,'original-object and window-dimension CPU/stack comparisons',flush=True)
faults=[]
for name in ['nonzero','protected','interop','current','dimensions']:
 run=subprocess.run([sys.executable,str(pathlib.Path(__file__).resolve()),name],capture_output=True,text=True,timeout=30)
 (out/(name+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==-6 and 'memory violation' in run.stderr,(name,run.returncode,run.stderr)
 faults.append(name)
report.update(passed=True,checked_faults=faults,sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/apple_render_dimensions.py',*barriers]},original_blocks_sha256=BODY_SHA,scope='Two actual renderer blocks; original nonzero target behavior preserved, default dimensions compared to original object dimensions, full CPU/stack comparison with only the deliberately absent object pointer normalized, five real checked invalid/protected reads. Component test; no playable-map acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
