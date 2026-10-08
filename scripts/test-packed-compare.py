#!/usr/bin/env python3
"""Reached packed conversion instructions against fresh x86 execution."""
import ctypes,hashlib,json,os,pathlib,random,struct,subprocess
from packed_compare_runtime_inputs import ROWS,translate
from checked_support import compile_support
from guard_checked_link import guard_link
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/packed-compare';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text('{"passed":false}\n')
fixture='%_cpu = type opaque\n'+''.join(f'define void @site_{pc:x}(%_cpu* %cpu) {{\n call void @halopad_trap_unimplemented(i32 {pc}) nounwind\n ret void\n}}\n' for pc,_,_,_,_ in ROWS)
ir=out/'fixture.ll';ir.write_text(translate(fixture))
for pc,_,_,_,_ in ROWS:
 for bad in [fixture.replace(str(pc),str(pc+1)),fixture+f'call void @halopad_trap_unimplemented(i32 {pc}) nounwind\n']:
  try:translate(bad)
  except AssertionError:pass
  else:raise AssertionError('accepted changed instruction site')
helper=R/'port/llasm-support/tribes_packed_compare.c';scalar=R/'port/llasm-support/tribes_sse.c';math=sse_inputs('macos')
sources=[helper,scalar,ir]+list(map(pathlib.Path,math['sources']))
objects,_=compile_support(sources,out/'checked',['-DPTROFS_64BIT']+math['compile_flags']);meta=json.loads((out/'checked/result.json').read_text())
entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
guarded,_=guard_link(entries,out/'guarded')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();flags=['clang','-isysroot',sdk,'-O2','-Wno-override-module','-DPTROFS_64BIT']
oracle_source=R/'tests/packed_compare_oracle.c';oracle=out/'oracle'
subprocess.run(flags+['-arch','x86_64','-mavx',str(oracle_source),'-o',str(oracle)],check=True)
rng=random.Random(25034);edges=[0,0x80000000,1,0x80000001,0x7fffff,0x800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0x7fa12345,0xffc12345,0xffa12345,0x3f800000,0xbf800000]
inputs=[];requests=bytearray()
for index,(pc,_,op,dest,source) in enumerate(ROWS):
 for sample in range(4096):
  state=bytearray(rng.getrandbits(3520).to_bytes(440,'little'));control=0x1f80|((sample%4)<<13)|(0x40 if sample&4 else 0)|(0x8000 if sample&8 else 0)|(sample%64)
  struct.pack_into('<I',state,304,control)
  for lane in range(4):
   left=edges[(sample//16+lane)%len(edges)] if sample<2048 else rng.getrandbits(32)
   right=edges[(sample+lane)%len(edges)] if sample<2048 else rng.getrandbits(32)
   struct.pack_into('<I',state,176+dest*16+lane*4,left);struct.pack_into('<I',state,176+source*16+lane*4,right)
  requests+=struct.pack('<II',int(op=='minps'),control)+state[176+dest*16:192+dest*16]+state[312+dest*16:328+dest*16]+state[176+source*16:192+source*16]+state[312+source*16:328+source*16]
  inputs.append((pc,dest,source,state))
ref=subprocess.run(['arch','-x86_64',str(oracle)],input=bytes(requests),capture_output=True,check=True).stdout
assert len(ref)==36*len(inputs);(out/'requests.bin').write_bytes(requests);(out/'reference.bin').write_bytes(ref)
observer=out/'abort-observer.c';observer.write_text('#include <signal.h>\n#include <unistd.h>\nstatic void *state;static int fd;static void stop(int n){(void)n;write(fd,state,440);_exit(99);}\nvoid capture_abort(void *p,int f){state=p;fd=f;signal(SIGABRT,stop);}\n')
report={}
for mode in ['normal','checked']:
 library=out/(mode+'.dylib');selected=guarded if mode=='checked' else list(map(str,sources))
 subprocess.run(flags+math['compile_flags']+['-dynamiclib',*selected,*math['libraries'],str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c'),str(observer),'-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));region=ctypes.create_string_buffer(12288);base=(ctypes.addressof(region)+4095)&~4095
 lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(base,8192)
 lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(base,8192)
 if mode=='checked':lib.tribes_memory_external_enable()
 cpu=ctypes.create_string_buffer(440)
 for row,(pc,dest,source,initial) in enumerate(inputs):
  state=bytearray(initial);struct.pack_into('<Q',state,112,base);r=ref[row*36:(row+1)*36];expected=bytearray(state)
  expected[304:308]=r[:4];expected[176+dest*16:192+dest*16]=r[4:20];expected[312+dest*16:328+dest*16]=r[20:36]
  ctypes.memmove(cpu,bytes(state),440);fn=getattr(lib,f'site_{pc:x}');fn.argtypes=[ctypes.c_void_p];fn(cpu)
  assert cpu.raw==bytes(expected),(mode,row,[(i,cpu.raw[i],expected[i]) for i in range(440) if cpu.raw[i]!=expected[i]])
 # Every lane can raise IE/DE; no destination or unrelated CPU state publishes.
 lib.capture_abort.argtypes=[ctypes.c_void_p,ctypes.c_int];faults=0
 for pc,_,op,dest,source in ROWS:
  for lane in range(4):
   for value,control,raised in [(0x7fc12345,0x1f00,1),(1,0x1e80,2)]:
    state=bytearray(440);struct.pack_into('<Q',state,112,base);struct.pack_into('<I',state,304,control);struct.pack_into('<I',state,176+source*16+lane*4,value)
    ctypes.memmove(cpu,bytes(state),440);rd,wr=os.pipe();child=os.fork()
    if not child:
     os.close(rd);lib.capture_abort(cpu,wr);getattr(lib,f'site_{pc:x}')(cpu);os._exit(1)
    os.close(wr);snapshot=os.read(rd,440);os.close(rd);_,status=os.waitpid(child,0);expected=bytearray(state);struct.pack_into('<I',expected,304,control|raised)
    assert os.WIFEXITED(status) and os.WEXITSTATUS(status)==99 and snapshot==bytes(expected),(mode,op,lane,status);faults+=1
 report[mode]=dict(passed=True,cases=len(inputs),unmasked_no_publication=faults,library_sha256=sha(library));print('PASS',mode,len(inputs),'x86 comparisons;',faults,'unmasked stops',flush=True)
paths=[pathlib.Path(__file__).resolve(),R/'scripts/packed_compare_runtime_inputs.py',helper,scalar,oracle_source,R/'port/llasm-support/tribes_sse.h',R/'port/llasm-support/llasm_cpu.h',R/'port/core/sse_arithmetic.c',R/'port/core/sse_arithmetic.h']
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},sites=[hex(r[0]) for r in ROWS],source_rejections=8,reference_sha256=sha(out/'reference.bin'),scope='Four original register CMPPS-LT/MINPS sites; full CPU and YMM preservation, fresh Rosetta oracle, per-lane IE/DE no-publication stops. Full engine execution remains separate.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
