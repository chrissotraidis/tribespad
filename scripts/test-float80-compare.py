#!/usr/bin/env python3
import argparse
import ctypes,hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser();parser.add_argument('--library',type=pathlib.Path);parser.add_argument('--output',type=pathlib.Path,default=R/'generated/softfloat/compare');args=parser.parse_args()
out=args.output;out.mkdir(parents=True,exist_ok=True)
library=args.library or out/'libcompare.dylib'
sources=['port/core/ext80.c','port/llasm-support/tribes_float80.c','tests/x87_result_slot.c']
if not args.library: subprocess.run(['clang','-arch','arm64','-O2','-std=c11','-Wall','-Wextra','-Werror','-DPTROFS_64BIT','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(R/'ref/softfloat/SoftFloat-3e/source/include'),'-dynamiclib',*[str(R/s) for s in sources],str(R/'generated/softfloat/macos/softfloat.a'),str(R/'generated/bochs-math/libmath.dylib'),'-o',str(library)],check=True)
subprocess.run(['clang','-arch','x86_64','-O2',str(R/'tests/float80_compare_oracle.c'),'-o',str(out/'oracle')],check=True)
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('st',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx_active',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
lib=ctypes.CDLL(str(library));lib.x87_fninit_void.argtypes=[ctypes.POINTER(CPU)]
values=[(0,0),(0,0x8000),(1<<63,0x3fff),((1<<63)+1,0x3fff),((1<<64)-1,0x3fff),(1<<63,0xbfff),(1,0),(1,0x8000),(1<<63,0),(1<<63,1),(1,1),(1,0x7fff),(1<<63,0x7fff),(1<<63,0xffff),(0xc000000000000001,0x7fff),(0x8000000000000001,0x7fff)]
rng=random.Random(25034);values += [(rng.getrandbits(63)|(1<<63),rng.randrange(1,0x7fff)|rng.choice([0,0x8000])) for _ in range(16)]
cases=[(mode,a,b) for mode in range(6) for a in values for b in values]
request=''.join(f'{mode} {a[0]:x} {a[1]:x} {b[0]:x} {b[1]:x}\n' for mode,a,b in cases)
reply=subprocess.run(['arch','-x86_64',str(out/'oracle')],input=request,text=True,capture_output=True,check=True);oracle=[int(l,16) for l in reply.stdout.splitlines()];assert len(oracle)==len(cases)
names=['x87_fcom_st','x87_fcomp_st','x87_fcompp_void','x87_fucom_st','x87_fucomp_st','x87_fucompp_void'];failures=[]
for (mode,a,b),expected in zip(cases,oracle):
 c=CPU();lib.x87_fninit_void(ctypes.byref(c));c.top=6;c.tag=0xfff;c.st[6],c.exp[6]=a;c.st[7],c.exp[7]=b;c.sw=0x28
 fn=getattr(lib,names[mode]);fn.argtypes=[ctypes.POINTER(CPU)]+([ctypes.c_int] if mode%3!=2 else [])
 original=list(c.st),list(c.exp);fn(ctypes.byref(c),*([1] if mode%3!=2 else []));actual=c.sw|(c.top<<11)
 assert original==(list(c.st),list(c.exp)),'comparison changed register contents'
 # Native sticky OE/PE deliberately seeded; an independent comparison cannot clear them.
 expected|=0x28
 if actual!=expected and len(failures)<12:failures.append({'helper':names[mode],'a':a,'b':b,'actual':hex(actual),'expected':hex(expected)})
report={'pass':not failures,'cases':len(cases),'failures':failures,'coverage':'FCOM/FUCOM, one/two pops, all raw 64 significand bits, NaNs, infinities, zeros, denormals and unsupported encodings; sticky flags','limits':['Masked exceptions only','Unmasked Windows exception delivery and full game execution remain pending'],'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest()}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert not failures
