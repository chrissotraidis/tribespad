#!/usr/bin/env python3
"""Check pinned dynamic immediate readers against x86 and the real barrier."""
import ctypes,hashlib,json,pathlib,random,struct,subprocess
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EBP
from checked_support import compile_support
from guard_checked_link import guard_link
from mission_literal_runtime_inputs import ROWS,reader_body,writer_body,verify_images
from renderer_batch_runtime_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/mission-literals';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text(json.dumps(dict(passed=False,state='running'))+'\n')
verify_images()
original_dll=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll'
fixtures=[];records=[]
for i,(path,name) in enumerate([(R/'generated/integration-review/string-patched-engine/va/engine.ll','loc_5EDC8A'),(R/'generated/integration-review/renderer-batch/engine.ll','loc_5EDC8A')]):
 body=definition(path.read_text(),name);new=reader_body(body)
 prefix=new[:new.index(';add tmpadr, ebx, 2148')]
 prefix=prefix.replace('define hidden fastcc void @'+name+'(','define void @mission_reader%d('%i,1)
 fixtures.append(prefix+'ret void\n}\n');records.append(dict(name=name,original_sha256=hashlib.sha256(body.encode()).hexdigest(),translated_sha256=hashlib.sha256(new.encode()).hexdigest()))
for pc,name,address,_,_ in ROWS:
 body=definition(original_dll.read_text(),name)
 replacement=writer_body(body,pc,address)
 assert replacement.replace(f'call void @qol_mission_literal_write(%_cpu* %cpu, i32 {pc}, i32 {address})',f'call void @tb_sse_store(%_cpu* %cpu, i32 0, i32 {address}, i32 0) nounwind')==body
 try:writer_body(body.replace(str(address),str(address+1)),pc,address)
 except AssertionError:pass
 else:raise AssertionError('accepted changed writer')
try:reader_body(body)
except AssertionError:pass
else:raise AssertionError('accepted unrelated reader')
types=next(l for l in original_dll.read_text().splitlines() if l.startswith('%_cpu = type'))
ir=out/'readers.ll';ir.write_text(types+'\n'+''.join(fixtures))
fixture=R/'tests/mission_literal_test.c';store=R/'tests/published_code_store.c';barrier=R/'port/core/guest_memory_checks.c'
objects,_=compile_support([ir,store],out/'checked',[])
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
guarded,_=guard_link(entries,out/'guarded')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
flags=['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-Wno-override-module','-Wall','-Wextra','-Werror']
external=R/'port/core/guest_external_checks.c'
report=dict(passed=False,readers=records,writer_source_rejections=3,reader_source_rejections=1)
for mode in ['normal','checked']:
 library=out/(mode+'.dylib');inputs=guarded if mode=='checked' else list(map(str,[ir,store]))
 subprocess.run(flags+['-dynamiclib',str(fixture),str(barrier),str(external),*inputs,'-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));lib.mission_fixture_init.restype=ctypes.c_uint64;base=lib.mission_fixture_init()
 if mode=='checked':
  lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64]
  lib.tribes_memory_external_configure(base,0x11000000);lib.tribes_memory_external_enable()
 update=lib.tribes_memory_write_mission_literal;update.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32]
 x86=Uc(UC_ARCH_X86,UC_MODE_32);x86.mem_map(0x5ed000,4096);x86.mem_map(0x20000,4096)
 expected=[3205622661,1058139013,1058139013];rng=random.Random(25034);cases=0
 # Change one field at a time. All three MOVs must see the current partial state,
 # before any update, after zero/NaN values, and after repeated mission calls.
 values=[0,0x80000000,0x7fc12345,0x7fa12345,0xff800000,0xbf13cd3a,0xffffffff]+[rng.getrandbits(32) for _ in range(32)]
 for step in range(1+len(values)*3):
  if step:
   index=(step-1)%3;value=values[(step-1)//3];pc,_,address,_,_=ROWS[index]
   assert update(base,pc,address,value)==1;expected[index]=value
  raw=b''.join(bytes([0xc7,0x45,d])+struct.pack('<I',v) for d,v in zip([0xcc,0xc4,0xc8],expected))
  x86.mem_write(0x5edc8a,raw)
  for index in range(2):
   cpu=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440)
   struct.pack_into('<I',cpu,20,0x20800);struct.pack_into('<Q',cpu,112,base);saved=cpu.raw
   canary=bytes(rng.getrandbits(8) for _ in range(128));ctypes.memmove(base+0x207c0,canary,128);x86.mem_write(0x207c0,canary)
   x86.reg_write(UC_X86_REG_EBP,0x20800);x86.emu_start(0x5edc8a,0x5edc9f,count=3)
   fn=getattr(lib,'mission_reader%d'%index);fn.argtypes=[ctypes.c_void_p];fn(cpu)
   assert ctypes.string_at(base+0x207c0,128)==bytes(x86.mem_read(0x207c0,128))
   assert cpu.raw==saved;assert ctypes.string_at(base+0x5edc8a,21)==raw;cases+=1
 report[mode]=dict(passed=True,cases=cases,library_sha256=sha(library));print('PASS:',mode,cases,'original x86/native reader and partial-update comparisons',flush=True)
exe=out/'barrier-test';subprocess.run(flags+[str(fixture),str(barrier),str(external),*guarded,'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/'barrier.log').write_text(run.stdout+run.stderr)
assert run.returncode==0 and run.stderr.count('write to published AOT code')==3,(run.returncode,run.stdout,run.stderr)
report['barrier']=dict(passed=True,ordinary_write_faults=3,output=run.stdout);print(run.stdout.strip(),flush=True)
exe=out/'tsan';subprocess.run(flags+['-O1','-g','-fsanitize=thread',str(fixture),str(barrier),str(store),'-o',str(exe)],check=True)
run=subprocess.run([str(exe),'no-fork'],capture_output=True,text=True,timeout=60);(out/'tsan.log').write_text(run.stdout+run.stderr)
assert not run.returncode and not run.stderr,(run.returncode,run.stdout,run.stderr)
report['tsan']=dict(passed=True,output=run.stdout);print(run.stdout.strip(),flush=True)
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/mission_literal_runtime_inputs.py',fixture,barrier,R/'port/core/guest_memory_checks.h',R/'tests/qol_native_bootstrap.c',R/'scripts/patch_runtime_inputs.py',R/'config/runtime/external-memory-contracts.json']},scope='Two native reader variants against original three x86 MOVs, partial/repeated arbitrary bit-pattern updates, CPU/stack canaries, actual phase/owner/opcode/permissions gates, three instrumented ordinary-write faults, concurrent guarded readers/writes under TSan. Fixture startup revisions exercise the barrier, not original engine startup. No rendered-map/gameplay claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
