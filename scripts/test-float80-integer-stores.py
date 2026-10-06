#!/usr/bin/env python3
"""Check integer stores and defined status against independent x86-64/Rosetta."""
import ctypes, hashlib, json, pathlib, random, re, struct, subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/softfloat/integer-stores';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources=[R/n for n in ['port/core/ext80.c','port/llasm-support/tribes_float80.c','tests/x87_result_slot.c']]
emitted_source=R/'generated/integration-review/string-publication-checkpoint-20261003/native-proof/llvm/engine-routed.ll'
emitted_text=emitted_source.read_text()
block=re.search(r'^define [^\n]+@loc_427D20\([^\n]*\n(.*?)^}',emitted_text,re.M|re.S)[1]
start=block.index(';    call x87_fistp_int64 $\n');end=block.index(';add tmpadr, ebp, -28\n',start)
instruction=block[start:end];assert len(re.findall('store i32 ',instruction))==2 and instruction.count('@x87_fistp_int64')==1
instruction=re.sub(r'%(\d+)',r'%v\1',instruction)
emitted=out/'emitted.ll';emitted.write_text(re.search(r'^%_cpu = type .*$',emitted_text,re.M)[0]+'\ndeclare i32 @x87_fistp_int64(%_cpu*)\ndefine void @emitted_fistp64(%_cpu* %cpu) {\n'+instruction+'ret void\n}\n')
sources.append(emitted)
deps=[R/'generated/softfloat/macos/softfloat.a',R/'generated/bochs-math/libmath.dylib']
library=out/'libintegerstores.dylib'
subprocess.run(['clang','-target','arm64-apple-macos26.0','-O2','-std=c11','-Wall','-Wextra','-Werror','-Wno-override-module','-DPTROFS_64BIT','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(R/'ref/softfloat/SoftFloat-3e/source/include'),'-dynamiclib',*map(str,sources+deps),'-o',str(library)],check=True)
oracle_source=R/'tests/float80_integer_store_oracle.c';oracle=out/'oracle'
subprocess.run(['clang','-arch','x86_64','-O2','-Wall','-Wextra','-Werror',str(oracle_source),'-o',str(oracle)],check=True)
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('st',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx_active',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
assert ctypes.sizeof(CPU)==440
lib=ctypes.CDLL(str(library));lib.x87_fninit_void.argtypes=[ctypes.POINTER(CPU)]
lib.emitted_fistp64.argtypes=[ctypes.POINTER(CPU)];lib.emitted_fistp64.restype=None
for n in ['x87_fistp_int16','x87_fist_int32','x87_fistp_int32','x87_fistp_int64']:
 getattr(lib,n).argtypes=[ctypes.POINTER(CPU)];getattr(lib,n).restype=ctypes.c_uint32
rng=random.Random(25034)
# Exact signed-integer limits, half-way ties and nearby raw extended values.
values=[(0,0),(1,0),(1<<63,0),(1,1),(1,0x3fff),(0,0x7fff),(1,0x7fff),(1<<63,0x7fff),(0xc000000000000001,0x7fff),(0x8000000000000001,0x7fff)]
def dyadic(n,shift):
 if not n:return (0,0)
 leading=n.bit_length()-1
 if leading>63:
  if n&((1<<(leading-63))-1):return None
  m=n>>(leading-63)
 else:m=n<<(63-leading)
 return m,0x3fff+leading-shift
for n in range(1,33):values.append(dyadic(n,2))
for width in [16,32,64]:
 for delta in range(-6,7):
  value=dyadic(2*((1<<(width-1))-1)+delta,1)
  if value:values.append(value)
for e in [1,0x3ffd,0x3ffe,0x3fff,0x4000,0x400d,0x400e,0x401d,0x401e,0x403d,0x403e,0x403f,0x7ffe]:
 for m in [1<<63,(1<<63)+1,(1<<64)-1]:values.append((m,e))
values += [(rng.getrandbits(63)|(1<<63),rng.choice([0x3ffe,0x4000,0x400e,0x401e,0x403d,0x403e])) for _ in range(64)]
values=sorted(set((m,e|sign) for m,e in values for sign in [0,0x8000]))
forms=[(16,1),(32,0),(32,1),(64,1)]
cases=[(width,pop,0x7f|(pc<<8)|(rc<<10),seed,0,m,e) for width,pop in forms for pc in [0,2,3] for rc in range(4) for seed in [0,0x221] for m,e in values]
cases += [(width,pop,0x37f,seed,1,1<<63,0x3fff) for width,pop in forms for seed in [0,0x221]]
# Include the independently captured original-engine frontier without editing
# its raw CPU state beyond the test-only guest result buffer mapping.
frontier=R/'generated/integration-review/integer-conversion-checkpoint-20261003'
frontier_meta=json.loads((frontier/'frontier.json').read_text());raw_cpu=(frontier/'cpu.bin').read_bytes();assert sha(frontier/'cpu.bin')==frontier_meta['cpu_sha256']
cases.append((64,1,frontier_meta['control'],frontier_meta['status'],0,frontier_meta['significand'],frontier_meta['sign_exponent']))
request=''.join(f'{w} {p} {cw:x} {s:x} {empty} {m:x} {e:x}\n' for w,p,cw,s,empty,m,e in cases)
(out/'oracle-input.txt').write_text(request)
reply=subprocess.run(['arch','-x86_64',str(oracle)],input=request,text=True,capture_output=True,check=True)
assert reply.stderr.strip()=='translated=1';(out/'oracle.log').write_text(reply.stderr+reply.stdout)
answers=[tuple(int(v,16) for v in line.split()) for line in reply.stdout.splitlines()];assert len(answers)==len(cases)
(out/'reference-cases.txt').write_text(''.join(f'{w} {p} {cw:x} {s:x} {empty} {m:x} {e:x} {bits:x} {sw:x}\n' for (w,p,cw,s,empty,m,e),(bits,sw) in zip(cases,answers)))
failures=[];buffer=ctypes.create_string_buffer(8);emitted_memory=ctypes.create_string_buffer(65544);emitted_cases=0
for index,(case,(bits,sw)) in enumerate(zip(cases,answers)):
 width,pop,cw,seed,empty,m,e=case;c=CPU();lib.x87_fninit_void(ctypes.byref(c));c.top=7;c.cw=cw;c.sw=seed;c.tag=0xffff if empty else 0x3fff;c.st[7]=m;c.exp[7]=e;c.offset=ctypes.addressof(buffer)-0x500000
 if index==len(cases)-1:
  c=CPU.from_buffer_copy(raw_cpu);c.offset=ctypes.addressof(buffer)-0x500000
 else:c.regs[:]=[rng.getrandbits(32) for _ in range(9)]
 before=bytes(c)
 fn=getattr(lib,'x87_fist'+('p' if pop else '')+'_int'+str(width));actual=fn(ctypes.byref(c))&((1<<width)-1)
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
 if width==64:
  # Exercise the unchanged emitted wide-result load/store sequence in a
  # bounded fixture. The direct-helper case above retains captured registers.
  ec=CPU.from_buffer_copy(before);ec.offset=ctypes.addressof(emitted_memory)-0x4f0000;ec.regs[5]=0x4f8000
  ctypes.memset(emitted_memory,0xa5,65544);ebefore=bytes(ec);lib.emitted_fistp64(ctypes.byref(ec))
  assert struct.unpack_from('<Q',emitted_memory,0x8000-24)[0]==bits
  assert struct.unpack_from('<Q',emitted_memory,65536)[0]==bits
  assert ((ec.sw|(ec.top<<11))&mask)==(sw&mask) and ec.top==0 and ec.tag==0xffff
  ea=bytearray(bytes(ec));ee=bytearray(ebefore)
  for lo,hi in [(36,44),(120,128),(144,148)]:ea[lo:hi]=ee[lo:hi]
  assert ea==ee
  expected_memory=bytearray(b'\xa5'*65544)
  for offset in [0x8000-24,65536]:struct.pack_into('<Q',expected_memory,offset,bits)
  assert emitted_memory.raw==expected_memory,'emitted store changed unrelated guest bytes'
  emitted_cases+=1
report=dict(passed=not failures,cases=len(cases),emitted_instruction_cases=emitted_cases,emitted_source_sha256=sha(emitted_source),failures=failures,oracle='independent x86-64 execution under Rosetta',library_sha256=sha(library),oracle_sha256=sha(oracle),oracle_log_sha256=sha(out/'oracle.log'),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+deps+[oracle_source,pathlib.Path(__file__).resolve()]},reference_cases_sha256=sha(out/'reference-cases.txt'),oracle_input_sha256=sha(out/'oracle-input.txt'),frontier_cpu_sha256=sha(frontier/'cpu.bin'),scope='16/32/64-bit integer stores in the implemented FIST/FISTP forms; all rounding and legal precision controls, both signs, sticky C1/exception seeds, selected boundaries and exceptional encodings, empty stack. Unmasked exception delivery and guest-memory fault rollback are not established.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert not failures
