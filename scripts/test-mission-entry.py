#!/usr/bin/env python3
"""Compile the reached original closure and compare its exact SQRTSS with x86."""
import ctypes,hashlib,json,os,pathlib,random,struct,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
from mission_entry_runtime_inputs import prepare,translate_sqrt,SQRT
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/mission-entry-test';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text(json.dumps(dict(passed=False,state='running'))+'\n')
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic'
sources,closure=prepare(native,out,False)
fixture='%_cpu = type opaque\ndefine void @mission_sqrtss(%_cpu* %cpu) {\n call void @halopad_trap_unimplemented(i32 '+str(SQRT)+') nounwind\n ret void\n}\n'
ir=out/'fixture.ll';ir.write_text(translate_sqrt(fixture))
for changed in [fixture.replace(str(SQRT),str(SQRT+1)),fixture+fixture,fixture.replace(' nounwind','')]:
 try:translate_sqrt(changed)
 except AssertionError:pass
 else:raise AssertionError('accepted changed original site')
math=sse_inputs('macos');helper=R/'port/llasm-support/tribes_sqrtss.c';adapter=list(map(pathlib.Path,math['sources']))
objects,_=compile_support([helper,ir]+adapter,out/'checked',math['compile_flags'])
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
guarded,_=guard_link(entries,out/'guarded')
closure_objects,_=compile_support([sources[0]],out/'closure-checked',[])
observer=out/'abort-observer.c';observer.write_text('''#include <signal.h>
#include <unistd.h>
static void *state;static int descriptor;
static void stopped(int n){(void)n;write(descriptor,state,440);_exit(99);}
void capture_abort(void *cpu,int fd){state=cpu;descriptor=fd;signal(SIGABRT,stopped);}
''')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
flags=['clang','-isysroot',sdk,'-O2','-Wno-override-module']
oracle_source=R/'tests/mission_sqrtss_oracle.c';oracle=out/'x86-oracle'
subprocess.run(flags+['-arch','x86_64','-mavx',str(oracle_source),'-o',str(oracle)],check=True)
rng=random.Random(25034)
edges=[0,0x80000000,1,0x80000001,0x7fffff,0x800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0x7fa12345,0xffc12345,0xffa12345,0x3f800000,0xbf800000,0x40000000,0x40800000]
inputs=[];requests=bytearray()
for sample in range(len(edges)*16+512):
 state=bytearray(rng.getrandbits(3520).to_bytes(440,'little'))
 control=0x1f80|((sample%4)<<13)|(0x40 if sample&4 else 0)|(0x8000 if sample&8 else 0)|(sample%64)
 struct.pack_into('<I',state,304,control);struct.pack_into('<I',state,192,edges[sample//16] if sample<len(edges)*16 else rng.getrandbits(32))
 requests+=struct.pack('<I',control)+state[192:208]+state[328:344];inputs.append(state)
run=subprocess.run(['arch','-x86_64',str(oracle)],input=bytes(requests),capture_output=True,check=True)
assert len(run.stdout)==36*len(inputs)
(out/'requests.bin').write_bytes(requests);(out/'reference.bin').write_bytes(run.stdout)
report=dict(passed=False,closure=closure,source_rejections=3,math=math['evidence'])
for mode in ['normal','checked']:
 library=out/(mode+'.dylib')
 selected=guarded if mode=='checked' else list(map(str,[helper,ir]+adapter))
 subprocess.run(flags+math['compile_flags']+['-dynamiclib',*selected,*math['libraries'],str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c'),str(observer),'-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library));region=ctypes.create_string_buffer(12288);base=(ctypes.addressof(region)+4095)&~4095
 lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(base,8192)
 lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(base,8192)
 if mode=='checked':lib.tribes_memory_external_enable()
 fn=lib.mission_sqrtss;fn.argtypes=[ctypes.c_void_p];lib.capture_abort.argtypes=[ctypes.c_void_p,ctypes.c_int]
 cpu=ctypes.create_string_buffer(440)
 for row,initial in enumerate(inputs):
  state=bytearray(initial);struct.pack_into('<Q',state,112,base);reference=run.stdout[row*36:(row+1)*36]
  expected=bytearray(state);expected[304:308]=reference[:4];expected[192:208]=reference[4:20];expected[328:344]=reference[20:36]
  ctypes.memmove(cpu,bytes(state),440);fn(cpu);assert cpu.raw==bytes(expected),(mode,row,[(j,cpu.raw[j],expected[j]) for j in range(440) if cpu.raw[j]!=expected[j]])
 faults=[]
 for value,control,raised in [(0xbf800000,0x1f00,1),(0x40000000,0x0f80,32)]:
  state=bytearray(inputs[0]);struct.pack_into('<Q',state,112,base);struct.pack_into('<I',state,192,value);struct.pack_into('<I',state,304,control)
  ctypes.memmove(cpu,bytes(state),440);read_fd,write_fd=os.pipe();child=os.fork()
  if not child:
   os.close(read_fd);lib.capture_abort(cpu,write_fd);fn(cpu);os._exit(1)
  os.close(write_fd);snapshot=os.read(read_fd,440);os.close(read_fd);_,status=os.waitpid(child,0)
  expected=bytearray(state);struct.pack_into('<I',expected,304,control|raised)
  assert os.WIFEXITED(status) and os.WEXITSTATUS(status)==99 and snapshot==bytes(expected),(mode,value,status)
  faults.append(hex(value))
 report[mode]=dict(passed=True,cases=len(inputs),unmasked_no_publication=faults,library_sha256=sha(library));print('PASS',mode,len(inputs),'SQRTSS x86/native comparisons and two unmasked stops',flush=True)
report.update(passed=True,closure_objects_sha256={str(p):sha(pathlib.Path(p)) for p in closure_objects},sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/mission_entry_runtime_inputs.py',R/'scripts/patch_runtime_inputs.py',helper,oracle_source,R/'port/core/sse_arithmetic.c',R/'port/core/sse_arithmetic.h',R/'port/llasm-support/llasm_cpu.h',R/'ref/qol/IFC22.dll']},oracle_sha256=sha(oracle),reference_sha256=sha(out/'reference.bin'),scope='81 additional original entries compiled with checked memory; five identical entries reused from the existing detour closure, including the existing SQRTSS support; existing blocks unchanged. Exact emitted SQRTSS against fresh Rosetta x86 bytes in normal and checked builds, full440 CPU and YMM preservation, all rounding/DAZ/FTZ combinations, sticky status, random bits and two unmasked destination-preserving stops. Full linked engine and rendered gameplay remain separate acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
