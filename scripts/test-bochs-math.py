#!/usr/bin/env python3
"""Characterize pinned pure math against raw x87; never conceal mismatches."""
import ctypes,hashlib,json,pathlib,random,struct,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/bochs-math/tests';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['clang','-arch','x86_64','-O2',str(R/'tests/float80_math_oracle.c'),'-o',str(out/'oracle')],check=True)
class Ext(ctypes.Structure):_fields_=[('m',ctypes.c_uint64),('e',ctypes.c_uint16)]
libpath=R/'generated/bochs-math/libmath.dylib';lib=ctypes.CDLL(str(libpath));fn=lib.tb_bo_math
fn.argtypes=[ctypes.c_uint,Ext,Ext,ctypes.c_uint16,ctypes.POINTER(Ext),ctypes.POINTER(ctypes.c_uint16),ctypes.POINTER(ctypes.c_uint64)];fn.restype=ctypes.c_int
def ext(d):
 bits=int.from_bytes(struct.pack('<d',d),'little');exp=(bits>>52)&2047;frac=bits&((1<<52)-1);sign=(bits>>48)&0x8000
 if exp:return ((1<<63)|(frac<<11),sign|exp+16383-1023)
 if not frac:return (0,sign)
 shift=53-frac.bit_length();return (frac<<(11+shift),sign|16383-1022-shift)
one=ext(1.);zero=ext(0.);rng=random.Random(25034);cases=[]
for rc in range(4):
 cw=0x37f|(rc<<10)
 for op in range(4):
  nums=[0.,-0.,1.,-1.,.5,-.5,.25,-.25,2**-100,-2**-100]+[rng.uniform(-1,1) for _ in range(32)]
  if op:nums += [2.,-2.,10.,-10.,2**63,-2**63]
  cases += [(op,cw,ext(x),zero) for x in nums]
 for op in [4,5,6,7]:
  pairs=[(a,b) for a in [0.,-0.,1.,-1.,.5,-.5,2.,-2.,10.,-10.] for b in [0.,-0.,1.,-1.,.5,-.5,2.,-2.,10.,-10.]]
  pairs += [(rng.uniform(.01,100) if op==4 else rng.uniform(-100,100),rng.uniform(-100,100)) for _ in range(32)]
  cases += [(op,cw,ext(a),ext(b)) for a,b in pairs]
request=''.join(f'{op} {cw:x} {a[0]:x} {a[1]:x} {b[0]:x} {b[1]:x}\n' for op,cw,a,b in cases)
reply=subprocess.run(['arch','-x86_64',str(out/'oracle')],input=request,text=True,capture_output=True,check=True)
oracle=[tuple(int(v,16) for v in line.split()) for line in reply.stdout.splitlines()];assert len(oracle)==len(cases)
families={str(op):{'cases':0,'value_mismatches':0,'status_mismatches':0,'max_same_exponent_ulp':0} for op in range(8)};failures=[];trace=[]
for (op,cw,a,b),(m,e,sw) in zip(cases,oracle):
 z=Ext();flags=ctypes.c_uint16();q=ctypes.c_uint64();rc=fn(op,Ext(*a),Ext(*b),cw,ctypes.byref(z),ctypes.byref(flags),ctypes.byref(q));iterations=0
 if op==6:
  while rc==1:
   iterations+=1;assert iterations<1024;rc=fn(op,z,Ext(*b),cw,ctypes.byref(z),ctypes.byref(flags),ctypes.byref(q))
 actual_flags=flags.value
 if op in [1,2,3] and rc==-1:actual_flags|=0x400
 if op==6 and rc==0:actual_flags=(actual_flags&~0x4700)|((q.value&4)<<6)|((q.value&2)<<13)|((q.value&1)<<9)
 # Trigonometric C2 is defined; FPREM defines quotient C0/C1/C3 at completion.
 mask=0x3f|0x200|(0x400 if op in [1,2,3] else 0)|(0x4500 if op==6 else 0)
 value_bad=(z.m,z.e)!=(m,e);status_bad=(actual_flags&mask)!=(sw&mask)
 trace.append({'op':op,'cw':cw,'a':a,'b':b,'result':[z.m,z.e],'flags':actual_flags,'oracle':[m,e,sw],'status_mask':mask})
 family=families[str(op)];family['cases']+=1;family['value_mismatches']+=value_bad;family['status_mismatches']+=status_bad
 if z.e==e:family['max_same_exponent_ulp']=max(family['max_same_exponent_ulp'],abs(z.m-m))
 if (value_bad or status_bad) and len(failures)<24:failures.append({'op':op,'cw':hex(cw),'a':a,'b':b,'actual':[hex(z.m),hex(z.e),hex(actual_flags)],'expected':[hex(m),hex(e),hex(sw&mask)],'value_mismatch':value_bad,'status_mismatch':status_bad})
report={'exact_pass':not failures,'cases':len(cases),'families':families,'examples':failures,'oracle':'x87 instructions through Rosetta; raw80 values and defined status bits','engine_selected':False,'library_sha256':hashlib.sha256(libpath.read_bytes()).hexdigest(),'limits':['Rosetta is not physical x86 hardware','Transcendental results can differ between implementations; mismatches require architectural accuracy checks before accepting a profile','Sampled normal domains; full exceptional and partial-reduction coverage pending']}
(out/'trace.json').write_text(json.dumps(trace)+'\n')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='examples'},indent=2))
