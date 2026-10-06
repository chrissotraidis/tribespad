#!/usr/bin/env python3
"""Compare native extended arithmetic against raw x87 results offline."""
import ctypes, hashlib, json, pathlib, random, subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/softfloat/tests';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['clang','-arch','arm64','-O2','-std=c11','-Wall','-Wextra','-Werror','-DSOFTFLOAT_FAST_INT64','-DLITTLEENDIAN=1','-DTHREAD_LOCAL=_Thread_local','-I',str(R/'ref/softfloat/SoftFloat-3e/source/include'),'-dynamiclib',str(R/'port/core/ext80.c'),str(R/'generated/softfloat/macos/softfloat.a'),'-o',str(out/'libext80.dylib')],check=True)
subprocess.run(['clang','-arch','x86_64','-O2',str(R/'tests/ext80_oracle.c'),'-o',str(out/'oracle')],check=True)
class Ext(ctypes.Structure):_fields_=[('m',ctypes.c_uint64),('e',ctypes.c_uint16)]
lib=ctypes.CDLL(str(out/'libext80.dylib'));fn=lib.tb_ext80_arithmetic
fn.argtypes=[ctypes.c_uint,Ext,Ext,ctypes.c_uint16,ctypes.POINTER(Ext),ctypes.POINTER(ctypes.c_uint16)];fn.restype=ctypes.c_int
rng=random.Random(25034)
# Include low significand bits binary64 cannot store and full exponent range.
values=[(0,0),(0,0x8000),(1<<63,0x3fff),((1<<63)+1,0x3fff),((1<<64)-1,0x3fff),((1<<63)+1,0xbfff),(1<<63,0x3ffe),(1<<63,0x4000),(1<<63,1),((1<<64)-1,0x7ffe)]
values += [(rng.getrandbits(63)|(1<<63),rng.choice([1,0x3ff0,0x3fff,0x4000,0x7ffe])|rng.choice([0,0x8000])) for _ in range(20)]
pairs=[(a,b) for a in values for b in values[:10]]
cases=[(op,0x7f|(pc<<8)|(rc<<10),a,b) for pc in [0,2,3] for rc in range(4) for op in range(5) for a,b in pairs]
request=''.join(f'{op} {cw:x} {a[0]:x} {a[1]:x} {b[0]:x} {b[1]:x}\n' for op,cw,a,b in cases)
p=subprocess.run(['arch','-x86_64',str(out/'oracle')],input=request,text=True,capture_output=True,check=True)
oracle=[tuple(int(v,16) for v in line.split()) for line in p.stdout.splitlines()];assert len(oracle)==len(cases)
failures=[]
for (op,cw,a,b),expected in zip(cases,oracle):
 z=Ext();flags=ctypes.c_uint16()
 assert fn(op,Ext(*a),Ext(*b),cw,ctypes.byref(z),ctypes.byref(flags))==1
 actual=(z.m,z.e,flags.value)
 if actual!=expected and len(failures)<20:failures.append({'op':op,'cw':hex(cw),'a':a,'b':b,'actual':actual,'expected':expected})
report={'pass':not failures,'cases':len(cases),'oracle':'raw x87 arithmetic through Rosetta, all four rounding modes and three precision controls','failures':failures,'engine_integrated':False,'limits':['Finite canonical operands only in differential census','C1 rounding direction, stack tags and unmasked exception delivery untested','Rosetta remains a translated oracle, not physical x86 hardware'],'library_sha256':hashlib.sha256((out/'libext80.dylib').read_bytes()).hexdigest()}
subprocess.run(['clang','-arch','arm64','-O2','-std=c11','-Wall','-Wextra','-Werror','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(R/'port/core'),'-I',str(R/'ref/softfloat/SoftFloat-3e/source/include'),str(R/'tests/ext80_threads.c'),str(R/'port/core/ext80.c'),str(R/'generated/softfloat/macos/softfloat.a'),'-o',str(out/'threads')],check=True)
thread_result=subprocess.run([str(out/'threads')],text=True,capture_output=True,check=True)
report['concurrent_operations']=40000
report['concurrent_threads']=8
report['thread_state_restoration']=thread_result.stdout.strip()
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert not failures
