#!/usr/bin/env python3
"""Check guarded math against the pinned implementation and protect guest outputs.

This proves instrumentation equivalence, not x87 architectural accuracy.
"""
import ctypes,hashlib,json,mmap,os,pathlib,random,signal,subprocess
from checked_bochs_inputs import checked_bochs_inputs
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/bochs-math/checked-tests';out.mkdir(exist_ok=True)
entries,build_sha=checked_bochs_inputs();objects,guard_sha=guard_link(entries,out/'guarded')
trusted=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
libpath=out/'libcheckedmath.dylib'
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-dynamiclib',*objects,*map(str,trusted),'-o',str(libpath)],check=True)
reference=R/'generated/bochs-math/libmath.dylib';reference_meta=json.loads(reference.with_name('result.json').read_text())
assert sha(reference)==reference_meta['native_bridge_sha256']
assert sha(R/'port/bochs-support/math_bridge.cc')==reference_meta['bridge_sha256']
assert sha(R/'port/bochs-support/config.h')==reference_meta['config_sha256']
class Ext(ctypes.Structure):_fields_=[('m',ctypes.c_uint64),('e',ctypes.c_uint16)]
P=ctypes.POINTER
libs=[ctypes.CDLL(str(reference)),ctypes.CDLL(str(libpath))]
for lib in libs:
 lib.tb_bo_arithmetic.argtypes=[ctypes.c_uint,Ext,Ext,ctypes.c_uint16,P(Ext),P(ctypes.c_uint16)];lib.tb_bo_arithmetic.restype=ctypes.c_int
 lib.tb_bo_math.argtypes=lib.tb_bo_arithmetic.argtypes+[P(ctypes.c_uint64)];lib.tb_bo_math.restype=ctypes.c_int
 lib.tb_bo_store_real.argtypes=[ctypes.c_uint,Ext,ctypes.c_uint16,P(ctypes.c_uint64),P(ctypes.c_uint16)];lib.tb_bo_store_real.restype=ctypes.c_int
 lib.tb_bo_store_integer.argtypes=lib.tb_bo_store_real.argtypes;lib.tb_bo_store_integer.restype=ctypes.c_int
checked=libs[1]
checked.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64]
checked.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64]
checked.tribes_memory_permissions_set.argtypes=[ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32]
memory=mmap.mmap(-1,65536,flags=mmap.MAP_SHARED|mmap.MAP_ANON,prot=mmap.PROT_READ|mmap.PROT_WRITE)
base=ctypes.addressof(ctypes.c_char.from_buffer(memory))
assert checked.tribes_memory_checks_configure(base,65536)==1
checked.tribes_memory_external_configure(base,65536);checked.tribes_memory_external_enable()
values=[(0,0),(0,0x8000),(1,0),(1<<63,0),(1<<63,1),(1<<63,0x3fff),(1<<63,0xbfff),((1<<64)-1,0x7ffe),(1<<63,0x7fff),(0xc000000000000001,0x7fff),(0x8000000000000001,0x7fff),(1,0x3fff)]
rng=random.Random(25034)
values += [(rng.getrandbits(64),rng.getrandbits(16)) for _ in range(80)]
def invoke(lib,kind,op,a,b,cw):
 result=Ext(0x12345678,0xabcd);flags=ctypes.c_uint16(0x4321);q=ctypes.c_uint64(0x87654321)
 args=[op,Ext(*a),Ext(*b),cw,ctypes.byref(result),ctypes.byref(flags)]
 if kind=='math':args.append(ctypes.byref(q))
 rc=getattr(lib,'tb_bo_'+kind)(*args)
 return rc,result.m,result.e,flags.value,q.value
counts={}
for kind,ops in [('arithmetic',range(6)),('math',range(9))]:
 count=0
 for pc in ([0,1,2,3] if kind=='arithmetic' else [3]):
  for rounding in range(4):
   for op in ops:
    for a in values:
     b=rng.choice(values);cw=0x7f|(pc<<8)|(rounding<<10)
     actual=invoke(checked,kind,op,a,b,cw);expected=invoke(libs[0],kind,op,a,b,cw)
     assert actual==expected,(kind,op,a,b,cw,actual,expected);count+=1
 counts[kind]=count
for kind,widths in [('store_real',[32,64,16]),('store_integer',[16,32,64,8])]:
 counts[kind]=0
 for width in widths:
  for rounding in range(4):
   for a in values:
    results=[]
    for lib in libs:
     bits=ctypes.c_uint64(0x87654321);flags=ctypes.c_uint16(0x4321)
     rc=getattr(lib,'tb_bo_'+kind)(width,Ext(*a),0x37f|(rounding<<10),ctypes.byref(bits),ctypes.byref(flags))
     results.append((rc,bits.value,flags.value))
    assert results[0]==results[1],(kind,width,rounding,a,results)
    counts[kind]+=1
# A real output in writable guest memory, with both enforcement layers enabled.
result_ptr=ctypes.cast(base+32,P(Ext));flags_ptr=ctypes.cast(base+64,P(ctypes.c_uint16));q_ptr=ctypes.cast(base+80,P(ctypes.c_uint64))
one=Ext(1<<63,0x3fff)
assert checked.tb_bo_arithmetic(0,one,one,0x37f,result_ptr,flags_ptr)==1
assert (result_ptr.contents.m,result_ptr.contents.e)==(1<<63,0x4000)
assert checked.tb_bo_math(1,Ext(),Ext(),0x37f,result_ptr,flags_ptr,q_ptr)==0
assert checked.tb_bo_store_real(32,one,0x37f,q_ptr,flags_ptr)==1
assert q_ptr.contents.value==0x3f800000 and flags_ptr.contents.value==0
assert checked.tb_bo_store_integer(64,one,0x37f,q_ptr,flags_ptr)==1
assert q_ptr.contents.value==1 and flags_ptr.contents.value==0
faults=[]
for kind,output,offset,access in [('arithmetic','result',32,1),('arithmetic','flags',32,1),('math','result',32,1),('math','flags',32,1),('math','quotient',32,1),('arithmetic','result',4092,1),('math','result',32,0)]:
 memory[:]=b'\xa5'*65536;before=memory[:]
 pid=os.fork()
 if pid==0:
  with open(out/f'fault-{len(faults)}.log','w') as f:os.dup2(f.fileno(),2)
  page=4096 if offset==4092 else 0
  assert checked.tribes_memory_permissions_set(page,4096,access)
  result=Ext();flags=ctypes.c_uint16();q=ctypes.c_uint64()
  args=[0,one,one,0x37f,ctypes.byref(result),ctypes.byref(flags)]
  if kind=='math':args.append(ctypes.byref(q))
  index,typ={'result':(4,Ext),'flags':(5,ctypes.c_uint16),'quotient':(6,ctypes.c_uint64)}[output]
  args[index]=ctypes.cast(base+offset,P(typ));getattr(checked,'tb_bo_'+kind)(*args);os._exit(90)
 _,status=os.waitpid(pid,0)
 assert os.WIFSIGNALED(status) and os.WTERMSIG(status)==signal.SIGABRT,(kind,output,status)
 assert memory[:]==before,(kind,output,'modified protected output')
 assert 'checked guest memory violation' in (out/f'fault-{len(faults)}.log').read_text()
 faults.append(dict(kind=kind,output=output,offset=offset,access=access))
for kind,width,output,offset in [('store_real',32,'bits',32),('store_real',64,'flags',32),('store_real',64,'bits',4092),('store_integer',16,'bits',32),('store_integer',32,'flags',32),('store_integer',64,'bits',4092)]:
 memory[:]=b'\xa5'*65536;before=memory[:];index=len(faults);pid=os.fork()
 if pid==0:
  with open(out/f'fault-{index}.log','w') as f:os.dup2(f.fileno(),2)
  assert checked.tribes_memory_permissions_set(4096 if offset==4092 else 0,4096,1)
  bits=ctypes.c_uint64();flags=ctypes.c_uint16();args=[width,one,0x37f,ctypes.byref(bits),ctypes.byref(flags)]
  args[3 if output=='bits' else 4]=ctypes.cast(base+offset,P(ctypes.c_uint64 if output=='bits' else ctypes.c_uint16))
  getattr(checked,'tb_bo_'+kind)(*args);os._exit(90)
 _,status=os.waitpid(pid,0)
 assert os.WIFSIGNALED(status) and os.WTERMSIG(status)==signal.SIGABRT
 assert memory[:]==before and 'checked guest memory violation' in (out/f'fault-{index}.log').read_text()
 faults.append(dict(kind=kind,width=width,output=output,offset=offset,access=1))
report=dict(passed=True,equivalence_cases=counts,protected_output_faults=faults,checked_build_sha256=build_sha,guard_build_sha256=guard_sha,library_sha256=sha(libpath),reference_library_sha256=sha(reference),sources_sha256={str(p.relative_to(R)):sha(p) for p in trusted+[pathlib.Path(__file__).resolve()]},scope='Exact value/status/return comparison to pinned pure math implementation with guards enabled, writable guest outputs and thirteen actual pre-write faults preserving shared memory, including float/double and integer conversion outputs. No x87 accuracy or gameplay acceptance claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
