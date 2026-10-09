#!/usr/bin/env python3
"""Check real stores and defined status against independent x86-64/Rosetta."""
import argparse
import ctypes, hashlib, json, pathlib, random, struct, subprocess
R=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--library',type=pathlib.Path);parser.add_argument('--output',type=pathlib.Path,default=R/'generated/softfloat/stores');args=parser.parse_args()
out=args.output;out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources=[R/n for n in ['port/core/ext80.c','port/llasm-support/tribes_float80.c','tests/x87_result_slot.c']]
deps=[R/'generated/softfloat/macos/softfloat.a',R/'generated/bochs-math/libmath.dylib']
library=args.library or out/'libstores.dylib'
if not args.library: subprocess.run(['clang','-target','arm64-apple-macos26.0','-O2','-std=c11','-Wall','-Wextra','-Werror','-DPTROFS_64BIT','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(R/'ref/softfloat/SoftFloat-3e/source/include'),'-dynamiclib',*map(str,sources+deps),'-o',str(library)],check=True)
oracle_source=R/'tests/float80_store_oracle.c';oracle=out/'oracle'
subprocess.run(['clang','-arch','x86_64','-O2','-Wall','-Wextra','-Werror',str(oracle_source),'-o',str(oracle)],check=True)
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('st',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx_active',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
assert ctypes.sizeof(CPU)==440
lib=ctypes.CDLL(str(library));lib.x87_fninit_void.argtypes=[ctypes.POINTER(CPU)]
for n in ['x87_fst_float','x87_fstp_float','x87_fst_double','x87_fstp_double']:
 getattr(lib,n).argtypes=[ctypes.POINTER(CPU)];getattr(lib,n).restype=ctypes.c_uint32
rng=random.Random(25034)
# Include halfway ties, both sides of rounding boundaries, underflow/overflow,
# extended denormals/pseudo-denormals, infinities, quiet/signaling NaNs and
# unsupported extended encodings. Every value is checked with both signs.
values=[(0,0),(1,0),(1<<63,0),(1,1),(1,0x3fff),(0,0x7fff),(1,0x7fff),(1<<63,0x7fff),(0xc000000000000001,0x7fff),(0x8000000000000001,0x7fff)]
for e in [1,0x3bcd,0x3bce,0x3c00,0x3f69,0x3f6a,0x3f81,0x3fff,0x407e,0x407f,0x43fe,0x43ff,0x7ffe]:
 for m in [1<<63,(1<<63)+1,(1<<63)+(1<<39),(1<<63)+(1<<40),(1<<63)+(1<<10),(1<<63)+(1<<11),(1<<64)-1]:values.append((m,e))
values += [(rng.getrandbits(63)|(1<<63),rng.choice([0x3f81,0x3fff,0x407e,0x3c00,0x43fe])) for _ in range(64)]
values=[(m,e|sign) for m,e in values for sign in [0,0x8000]]
cases=[(width,pop,0x7f|(pc<<8)|(rc<<10),seed,0,m,e) for width in [32,64] for pop in [0,1] for pc in [0,2,3] for rc in range(4) for seed in [0,0x221] for m,e in values]
cases += [(width,pop,0x37f,seed,1,1<<63,0x3fff) for width in [32,64] for pop in [0,1] for seed in [0,0x221]]
request=''.join(f'{w} {p} {cw:x} {s:x} {empty} {m:x} {e:x}\n' for w,p,cw,s,empty,m,e in cases)
reply=subprocess.run(['arch','-x86_64',str(oracle)],input=request,text=True,capture_output=True,check=True)
assert reply.stderr.strip()=='translated=1';(out/'oracle.log').write_text(reply.stderr+reply.stdout)
answers=[tuple(int(v,16) for v in line.split()) for line in reply.stdout.splitlines()];assert len(answers)==len(cases)
failures=[];buffer=ctypes.create_string_buffer(8)
for case,(bits,sw) in zip(cases,answers):
 width,pop,cw,seed,empty,m,e=case;c=CPU();lib.x87_fninit_void(ctypes.byref(c));c.top=7;c.cw=cw;c.sw=seed;c.tag=0xffff if empty else 0x3fff;c.st[7]=m;c.exp[7]=e;c.offset=ctypes.addressof(buffer)-0x500000
 c.regs[:]=[rng.getrandbits(32) for _ in range(9)];before=bytes(c)
 fn=getattr(lib,'x87_fst'+('p' if pop else '')+('_float' if width==32 else '_double'));actual=fn(ctypes.byref(c))
 if width==64:
  assert actual==0x500000;actual=struct.unpack('<Q',buffer.raw)[0]
 # C0/C2/C3 are architecturally undefined; include C1, all exceptions, SF,
 # TOP, ES and B. Seeded sticky flags must survive exact conversions.
 mask=0xbaff
 if actual!=bits or ((c.sw|(c.top<<11))&mask)!=(sw&mask):
  if len(failures)<20:failures.append(dict(case=case,actual=hex(actual),expected=hex(bits),actual_sw=hex(c.sw|(c.top<<11)),expected_sw=hex(sw)))
 assert c.top==(0 if pop else 7) and c.tag==(0xffff if pop or empty else 0x3fff)
 after=bytearray(bytes(c));expected=bytearray(before)
 for lo,hi in [(36,44),(144,148)]+([(120,128)] if width==64 else []):after[lo:hi]=expected[lo:hi]
 assert after==expected,'store changed unrelated CPU state'
report=dict(passed=not failures,cases=len(cases),failures=failures,oracle='independent x86-64 execution under Rosetta',library_sha256=sha(library),oracle_sha256=sha(oracle),oracle_log_sha256=sha(out/'oracle.log'),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+deps+[oracle_source,pathlib.Path(__file__).resolve()]},scope='Float/double FST/FSTP; all rounding and legal precision controls, both signs, sticky C1/exception seeds, selected boundaries and exceptional encodings, empty stack. Unmasked exception delivery and guest-memory fault rollback are not established.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert not failures
