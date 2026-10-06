#!/usr/bin/env python3
"""FRNDINT runtime versus raw x86 execution, with the actual checked math link."""
import ctypes,hashlib,json,mmap,os,pathlib,random,signal,subprocess
from checked_support import compile_support
from checked_bochs_inputs import checked_bochs_inputs
from guard_checked_link import guard_link
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/round-integer';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('st',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('reserved',ctypes.c_uint32),('ymm',(ctypes.c_uint8*16)*8)]
assert ctypes.sizeof(CPU)==440
sources=[R/'port/llasm-support/tribes_float80.c',R/'tests/x87_result_slot.c'];math=sse_inputs('macos');flags=['-DPTROFS_64BIT',*math['compile_flags']]
trusted=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
bo=R/'generated/bochs-math';bm=json.loads((bo/'result.json').read_text());assert sha(R/'port/bochs-support/math_bridge.cc')==bm['bridge_sha256'] and sha(bo/'libmath.dylib')==bm['native_bridge_sha256']
normal=out/'normal.dylib';subprocess.run(['clang','-O2','-dynamiclib',*flags,*map(str,sources+trusted),*math['libraries'],str(bo/'libmath.dylib'),'-o',str(normal)],check=True)
objects,_=compile_support(sources,out/'checked',flags);cm=json.loads((out/'checked/result.json').read_text());entries=[]
for obj,(name,row) in zip(objects,cm['objects'].items()):entries.append(dict(object=obj,object_sha256=sha(pathlib.Path(obj)),bitcode=str(out/'checked'/(name+'.bc')),bitcode_sha256=row['bitcode_sha256']))
math_entries,_=checked_bochs_inputs();objects,_=guard_link(entries+math_entries,out/'guarded');checked=out/'checked.dylib'
subprocess.run(['clang','-dynamiclib',*objects,*map(str,trusted),*math['libraries'],'-o',str(checked)],check=True)
subprocess.run(['clang','-arch','x86_64','-O2',str(R/'tests/float80_round_oracle.c'),'-o',str(out/'oracle')],check=True)
rng=random.Random(25034)
values=[(0,0),(0,0x8000),(1,0),(1,0x8000),(1<<63,0),(1,1),(1,0x7fff),(1<<63,0x7fff),(1<<63,0xffff),(0xc000000000000001,0x7fff),(0x8000000000000001,0x7fff)]
values += [(m,e|sign) for m,e in [(1<<63,0x3ffe),(0xc000000000000000,0x3ffe),(1<<63,0x3fff),(0xaaaaaaaaaaaaa800,0x3fff),(0xc000000000000000,0x3fff),(0xa000000000000000,0x4000),((1<<64)-1,0x403d),(1<<63,0x403e)] for sign in [0,0x8000]]
values += [(rng.getrandbits(63)|(1<<63),rng.randrange(0x3fc0,0x4040)|rng.choice([0,0x8000])) for _ in range(50)]
cases=[(0x7f|(pc<<8)|(rc<<10),seed,0,m,e) for pc in range(4) for rc in range(4) for seed in [0,0x4204] for m,e in values]+[(0x37f,seed,1,0,0) for seed in [0,0x4204]]
request=''.join('%x %x %d %x %x\n'%c for c in cases);run=subprocess.run(['arch','-x86_64',str(out/'oracle')],input=request,text=True,capture_output=True,check=True)
expected=[tuple(int(v,16) for v in line.split()) for line in run.stdout.splitlines()];assert len(expected)==len(cases)
(out/'vectors.txt').write_text(''.join('%x %x %d %x %x %x %x %x\n'%(*c,*v) for c,v in zip(cases,expected)))
reports=[]
for binary in [normal,checked]:
 lib=ctypes.CDLL(str(binary));lib.x87_frndint_void.argtypes=[ctypes.POINTER(CPU)]
 lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_permissions_set.argtypes=[ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32]
 memory=mmap.mmap(-1,65536);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));assert lib.tribes_memory_checks_configure(base,65536);lib.tribes_memory_external_configure(base,65536)
 if binary==checked:lib.tribes_memory_external_enable()
 for case,expect in zip(cases,expected):
  cw,seed,empty,m,e=case;c=CPU();ctypes.memset(ctypes.byref(c),0xa5,440);c.top=7;c.cw=cw;c.sw=seed;c.tag=0xffff if empty else 0x3fff;c.extended=1;c.st[7]=m;c.exp[7]=e;before=bytes(c)
  lib.x87_frndint_void(ctypes.byref(c));actual=(c.st[7],c.exp[7],(c.sw|(c.top<<11))&0x3aff)
  assert actual==(expect[0],expect[1],expect[2]&0x3aff),(binary.name,case,actual,expect)
  ztag=1 if not (c.exp[7]&0x7fff) and not c.st[7] else 2 if not (c.exp[7]&0x7fff) or (c.exp[7]&0x7fff)==0x7fff or not(c.st[7]>>63) else 0
  assert c.tag==0x3fff|(ztag<<14)
  c.st[7]=m;c.exp[7]=e;c.sw=seed;c.tag=0xffff if empty else 0x3fff;assert bytes(c)==before
 # Unserviced precision exceptions must stop; the checked CPU must honor page guards.
 for fault in ['precision']+(['protected_cpu'] if binary==checked else []):
  pid=os.fork()
  if pid==0:
   with open(out/(binary.stem+'-'+fault+'.log'),'w') as f:os.dup2(f.fileno(),2)
   c=CPU.from_address(base);ctypes.memset(base,0,440);c.top=0;c.tag=0xfffc;c.extended=1;c.cw=0x35f;c.st[0]=0xc000000000000000;c.exp[0]=0x3fff
   if fault=='protected_cpu':assert lib.tribes_memory_permissions_set(0,4096,0)
   lib.x87_frndint_void(ctypes.byref(c));os._exit(99)
  _,status=os.waitpid(pid,0);assert os.WIFSIGNALED(status) and os.WTERMSIG(status)==signal.SIGABRT,(fault,status)
 reports.append(dict(mode=binary.stem,cases=len(cases),binary_sha256=sha(binary)));print(binary.stem,'PASS',len(cases),'FRNDINT result/status/CPU comparisons',flush=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=reports,vectors_sha256=sha(out/'vectors.txt'),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[R/'tests/float80_round_oracle.c',pathlib.Path(__file__).resolve(),R/'port/bochs-support/math_bridge.cc']},scope='Actual FRNDINT helper, Mac normal/checked, masked values/status against Rosetta x86; unserviced precision stops and protected checked CPU. No physical x86 or mobile/gameplay acceptance.'),indent=2)+'\n')
