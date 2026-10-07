#!/usr/bin/env python3
"""Entered neighboring math kernels: checked ARM64 against fresh x86 bytes."""
import ctypes,hashlib,json,os,pathlib,random,struct,subprocess,sys
import pefile
from checked_support import compile_support
from guard_checked_link import guard_link
from patch_runtime_inputs import MATH_MODEL_SITES,translate_math_model
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/math-next-kernel';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
image=R/'ref/qol/IFC22.dll';pe=pefile.PE(str(image))
fixture='%_cpu = type opaque\n'+''.join('define void @site_%x(%%_cpu* %%cpu) {\n call void @halopad_trap_unimplemented(i32 %d) nounwind\n ret void\n}\n'%(pc,pc) for pc,*_ in MATH_MODEL_SITES)
source=out/'fixture.ll';source.write_text(translate_math_model(fixture,image))
rejections=0
for pc,*_ in MATH_MODEL_SITES:
 needle='call void @halopad_trap_unimplemented(i32 %d) nounwind'%pc
 for bad in [fixture.replace(needle,''),fixture+needle]:
  try:translate_math_model(bad,image)
  except AssertionError:rejections+=1
  else:raise AssertionError('accepted changed trap site')
bad=out/'changed.dll';data=bytearray(image.read_bytes());data[-1]^=1;bad.write_bytes(data)
try:
 try:translate_math_model(fixture,bad)
 except AssertionError:rejections+=1
 else:raise AssertionError('accepted changed image')
finally:bad.unlink()
math=sse_inputs('macos');sources=[R/'port/llasm-support/tribes_sse.c',source]+list(map(pathlib.Path,math['sources']))
objects,_=compile_support(sources,out/'checked',['-DPTROFS_64BIT',*math['compile_flags']]);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,_=guard_link(entries,out/'guarded')
# Test-only observer records CPU state when a helper deliberately aborts.
observer=out/'abort-observer.c';observer.write_text('''#include <signal.h>
#include <unistd.h>
static void *state;static int descriptor;
static void stopped(int signal_number){(void)signal_number;write(descriptor,state,440);_exit(99);}
void capture_abort(void *cpu,int fd){state=cpu;descriptor=fd;signal(SIGABRT,stopped);}
''')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();library=out/'checked.dylib'
subprocess.run(['clang','-isysroot',sdk,'-dynamiclib',*objects,*math['libraries'],str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c'),str(observer),'-o',str(library)],check=True)
loads='\\n'.join('movdqu %d(%%[vectors]),%%%%xmm%d'%(16*i,i) for i in range(8))
stores='\\n'.join('movdqu %%%%xmm%d,%d(%%[vectors])'%(i,16*i) for i in range(8))
cases=[]
for i,(pc,raw,op,_,__) in enumerate(MATH_MODEL_SITES):
 actual=pe.get_data(pc-0x10000000,len(bytes.fromhex(raw)));assert actual.hex()==raw
 instruction=('stmxcsr %[status]' if op=='save' else f'mulpd %[operand], %%xmm{_}' if op.startswith('mulmem') else '.byte '+','.join('0x%02x'%v for v in actual))
 cases.append('case %d: EXEC("%s");break;'%(i,instruction))
oracle_source=out/'x86-oracle.c';oracle_source.write_text('''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>
typedef struct {uint32_t site,control,a,d;unsigned char vectors[128];unsigned char operand[16];} __attribute__((aligned(16))) Request;
typedef struct {uint32_t a,d,status;unsigned char vectors[128];} Response;
int main(void){int translated=0;size_t size=sizeof translated;
assert(!sysctlbyname("sysctl.proc_translated",&translated,&size,0,0)&&translated==1);
Request request;Response response;
while(fread(&request,sizeof request,1,stdin)==1){
uint32_t a=request.a,d=request.d,status=0;
#define EXEC(code) __asm__ volatile("ldmxcsr %[control]\\n'''+loads+'\\n" code "\\n'+stores+'''\\nstmxcsr %[status]"
: "+a"(a),"+d"(d),[status]"=m"(status)
: [control]"m"(request.control),[vectors]"r"(request.vectors),[operand]"m"(request.operand)
: "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory")
switch(request.site){'''+''.join(cases)+'''default:assert(0);}
response.a=a;response.d=d;response.status=status;
for(unsigned i=0;i<128;++i)response.vectors[i]=request.vectors[i];
assert(fwrite(&response,sizeof response,1,stdout)==1);
}return 0;}
''')
# Macro continuation is explicit so the constraints belong to EXEC.
text=oracle_source.read_text().replace('\\nstmxcsr %[status]"\n:', '\\nstmxcsr %[status]" \\\n:').replace('"=m"(status)\n:', '"=m"(status) \\\n:').replace('"m"(request.operand)\n:', '"m"(request.operand) \\\n:');oracle_source.write_text(text)
oracle=out/'x86-oracle';subprocess.run(['clang','-isysroot',sdk,'-arch','x86_64','-O0',str(oracle_source),'-o',str(oracle)],check=True)
lib=ctypes.CDLL(str(library));lib.capture_abort.argtypes=[ctypes.c_void_p,ctypes.c_int]
native=ctypes.CDLL(None)
native.mmap.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_long];native.mmap.restype=ctypes.c_void_p
native.mprotect.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int]
base=native.mmap(None,0x100000000,0,0x1002,-1,0);assert base and base!=ctypes.c_void_p(-1).value
page=os.sysconf('SC_PAGE_SIZE')
for address in [0,0x101cb420//page*page]:assert native.mprotect(base+address,page,3)==0
lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(base,0x100000000)
lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(base,0x100000000);lib.tribes_memory_external_enable()
lib.tribes_memory_permissions_set.argtypes=[ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32]
assert lib.tribes_memory_permissions_set(0,0x100000000,0)
for address in [0,0x101cb420//page*page]:assert lib.tribes_memory_permissions_set(address,page,3)
functions=[]
for pc,*_ in MATH_MODEL_SITES:
 fn=getattr(lib,'site_%x'%pc);fn.argtypes=[ctypes.c_void_p];functions.append(fn)
captured=R/'generated/integration-review/math-mxcsr-save/execution/next-instruction/cpu.bin';capture=captured.read_bytes();assert len(capture)==440
next_captured=R/'generated/integration-review/math-model-instructions/execution/next-instruction/cpu.bin';next_capture=next_captured.read_bytes();assert len(next_capture)==440
captured_xmm1=struct.unpack_from('<Q',capture,192)[0]
bits=lambda x:struct.unpack('<Q',struct.pack('<d',x))[0]
edges=[0,0x8000000000000000,1,0x8000000000000001,0xfffffffffffff,0x10000000000000,0x7fefffffffffffff,0xffefffffffffffff,0x7ff0000000000000,0xfff0000000000000,0x7ff8000000000123,0x7ff0000000000123,0xfff8000000004321,0xfff0000000004321,captured_xmm1]+[bits(x) for x in [.5,-.5,1.,-1.,1.5,-1.5,2.5,-2.5,2147483647.,2147483647.5,2147483648.,-2147483648.,-2147483648.5,-2147483648.75,2.**31,2.**32,2.**-1022,2.**-1021]]
rng=random.Random(25034);inputs=[];requests=bytearray();memory_before=bytes(rng.getrandbits(8) for _ in range(8192))
for index,(pc,raw,op,dest,src) in enumerate(MATH_MODEL_SITES):
 for sample in range(len(edges)*16+128):
  state=bytearray(capture if sample==0 else rng.getrandbits(3520).to_bytes(440,'little'))
  control=0x1f80|((sample%4)<<13)|(0x40 if sample&4 else 0)|(0x8000 if sample&8 else 0)|(sample%64)
  struct.pack_into('<I',state,16,4090);struct.pack_into('<Q',state,112,base);struct.pack_into('<I',state,304,control)
  operand=bytes(rng.getrandbits(8) for _ in range(16))
  if op in ['mul','sub','cvtsd','mulmemabs','mulmemeax']:
   a=edges[sample//16] if sample<len(edges)*16 else rng.getrandbits(64)
   b=edges[(sample//16+7)%len(edges)] if sample<len(edges)*16 else rng.getrandbits(64)
   if op=='cvtsd':struct.pack_into('<Q',state,176+src*16,a)
   else:
    struct.pack_into('<2Q',state,176+dest*16,a,b)
    right=(edges[(sample//16+1)%len(edges)] if sample<len(edges)*16 else rng.getrandbits(64),edges[(sample//16+15)%len(edges)] if sample<len(edges)*16 else rng.getrandbits(64))
    if op.startswith('mulmem'):operand=struct.pack('<2Q',*right)
    elif src!=dest:struct.pack_into('<2Q',state,176+src*16,*right)
  if op=='mulmemeax':struct.pack_into('<I',state,0,([0x10,0xff0,0x1000][sample%3]-src)&0xffffffff)
  requests+=struct.pack('<4I',index,control,*struct.unpack_from('<I',state,0),*struct.unpack_from('<I',state,8))+state[176:304]+operand
  inputs.append((index,state,operand))
 # Replay both reached CPU/control/vector states separately.
 for actual_capture in [capture,next_capture,(R/'generated/integration-review/apple-render-dimensions/execution/cpu.bin').read_bytes()]:
  state=bytearray(actual_capture);struct.pack_into('<I',state,16,4090);struct.pack_into('<Q',state,112,base)
  control=struct.unpack_from('<I',state,304)[0]
  operand=struct.pack('<2d',.5,-2.)
  if op=='mulmemeax':struct.pack_into('<I',state,0,0xff0-src)
  requests+=struct.pack('<4I',index,control,*struct.unpack_from('<I',state,0),*struct.unpack_from('<I',state,8))+state[176:304]+operand
  inputs.append((index,state,operand))
(out/'requests.bin').write_bytes(requests)
run=subprocess.run(['arch','-x86_64',str(oracle)],input=bytes(requests),capture_output=True,check=True);assert len(run.stdout)==len(inputs)*140
(out/'reference.bin').write_bytes(run.stdout);(out/'reference.stderr').write_bytes(run.stderr)
per_site={};cpu=ctypes.create_string_buffer(440)
for row,(index,state,operand) in enumerate(inputs):
 pc,raw,op,dest,src=MATH_MODEL_SITES[index];reference=run.stdout[row*140:(row+1)*140];a,d,control=struct.unpack_from('<3I',reference);expected=bytearray(state)
 struct.pack_into('<I',expected,0,a);struct.pack_into('<I',expected,8,d);struct.pack_into('<I',expected,304,control);expected[176:304]=reference[12:]
 ctypes.memmove(cpu,bytes(state),440);ctypes.memmove(base,memory_before,8192)
 address=src if op=='mulmemabs' else (struct.unpack_from('<I',state)[0]+src)&0xffffffff if op=='mulmemeax' else None
 if address is not None:ctypes.memmove(base+address,operand,16)
 functions[index](cpu)
 assert cpu.raw==bytes(expected),(hex(pc),row,[(j,cpu.raw[j],expected[j]) for j in range(440) if cpu.raw[j]!=expected[j]])
 expected_memory=bytearray(memory_before)
 if op=='save':struct.pack_into('<I',expected_memory,4094,control)
 if address is not None:
  assert ctypes.string_at(base+address,16)==operand
  if address<8192:expected_memory[address:address+16]=operand
 assert ctypes.string_at(base,8192)==bytes(expected_memory),(hex(pc),'memory')
 per_site[hex(pc)]=per_site.get(hex(pc),0)+1
faults=[]
for name,index,control,values,raised in [('conversion-precision',0,0x0f80,[bits(.5)],32),('conversion-invalid',0,0x1f00,[0x7ff8000000000011],1),('multiply-second-lane',1,0x1f00,[bits(1),0x7ff0000000000000,bits(2),0],1),('subtract-second-lane',3,0x1f00,[bits(1),0x7ff0000000000000,bits(2),0x7ff0000000000000],1)]:
 state=bytearray(capture);struct.pack_into('<Q',state,112,base);struct.pack_into('<I',state,304,control);pc,raw,op,dest,src=MATH_MODEL_SITES[index]
 if op=='cvtsd':struct.pack_into('<Q',state,176+src*16,values[0])
 else:struct.pack_into('<2Q',state,176+dest*16,*values[:2]);struct.pack_into('<2Q',state,176+src*16,*values[2:])
 ctypes.memmove(cpu,bytes(state),440);read_fd,write_fd=os.pipe();child=os.fork()
 if child==0:
  os.close(read_fd);lib.capture_abort(cpu,write_fd);functions[index](cpu);os._exit(1)
 os.close(write_fd);snapshot=os.read(read_fd,440);os.close(read_fd);_,status=os.waitpid(child,0)
 expected=bytearray(state);struct.pack_into('<I',expected,304,control|raised)
 assert os.WIFEXITED(status) and os.WEXITSTATUS(status)==99 and snapshot==bytes(expected),(name,status,snapshot.hex())
 faults.append(name)
memory_faults=[]
mem_index=next(i for i,row in enumerate(MATH_MODEL_SITES) if row[2]=='mulmemeax')
for name,address,permission,control,operand in [
 ('aligned-protected',4096,0,0x1f80,struct.pack('<2d',1.,2.)),
 ('unaligned',1,3,0x1f80,struct.pack('<2d',1.,2.)),
 ('unmasked-memory-second-lane',0x1000,3,0x1f00,struct.pack('<2Q',bits(2.),0))]:
 state=bytearray(capture);struct.pack_into('<Q',state,112,base);struct.pack_into('<I',state,0,(address-0x90)&0xffffffff);struct.pack_into('<I',state,304,control)
 struct.pack_into('<2Q',state,192,bits(1.),0x7ff0000000000000)
 ctypes.memmove(base+address,operand,16);ctypes.memmove(cpu,bytes(state),440)
 read_fd,write_fd=os.pipe();child=os.fork()
 if child==0:
  os.close(read_fd);lib.capture_abort(cpu,write_fd)
  assert lib.tribes_memory_permissions_set(address//4096*4096,4096,permission)
  functions[mem_index](cpu);os._exit(1)
 os.close(write_fd);snapshot=os.read(read_fd,440);os.close(read_fd);_,status=os.waitpid(child,0)
 expected=bytearray(state)
 if name.startswith('unmasked'):struct.pack_into('<I',expected,304,control|1)
 assert os.WIFEXITED(status) and os.WEXITSTATUS(status)==99 and snapshot==bytes(expected),(name,status,snapshot.hex())
 memory_faults.append(name)
report=dict(passed=True,memory_faults=memory_faults,cases=len(inputs),sites=len(MATH_MODEL_SITES),per_site=per_site,source_rejections=rejections,unmasked_destination_preservation=faults,captured_cpu_sha256=sha(captured),next_captured_cpu_sha256=sha(next_captured),captured_conversion_input=hex(captured_xmm1),sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/patch_runtime_inputs.py',R/'port/llasm-support/tribes_sse.c',R/'port/llasm-support/tribes_sse.h',image]},oracle_source_sha256=sha(oracle_source),oracle_binary_sha256=sha(oracle),requests_sha256=sha(out/'requests.bin'),reference_sha256=sha(out/'reference.bin'),library_sha256=sha(library),math=math['evidence'],scope='Fifty pinned emitted sites against fresh Rosetta x86 instructions; two memory operands relocated to aligned oracle storage. Original guest effective addresses independently exercised, including EAX wrap and page-edge reads. All rounding/DAZ/FTZ combinations, boundaries, NaN/invalid/precision/sticky status and register aliases. Full440 CPU including flags/x87/YMM and8192 memory canaries. Four deliberate unmasked stops preserve destination. Fallback stack save independently checked with existing protected-store test. No precise unmasked Windows delivery, complete math routine, gameplay or physical Intel claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(inputs),'checked ARM64/x86 cases across',len(per_site),'original math sites;',len(faults),'unmasked destination-preservation stops')
