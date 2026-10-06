#!/usr/bin/env python3
"""Differential x86 checks: CALL tails and whole blocks with removed calls."""
import ctypes,hashlib,json,mmap,pathlib,random,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-nested';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((out/'preparation.json').read_text())
for key,root in [('inputs_sha256',R),('outputs_sha256',out)]:
 for n,h in m[key].items():assert sha(root/n)==h,n
subprocess.run(['clang','-O2','-Wno-override-module','-c',str(out/'engine.ll'),'-o',str(out/'engine-test.o')],check=True)
targets=sorted({t for r in m['records'] for t in r['targets']});bridge=out/'bridge.ll';bridge.write_text('@observed = global i32 0\n'+''.join('define hidden fastcc void @loc_%X(ptr %%cpu) {store i32 %d, ptr @observed\n ret void\n}\n'%(n,n) for n in targets))
libpath=out/'test.dylib';subprocess.run(['clang','-O2','-Wno-override-module','-dynamiclib',str(out/'tails.ll'),str(bridge),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));seen=ctypes.c_uint32.in_dll(lib,'observed');memory=mmap.mmap(-1,0x11000000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));rng=random.Random(0x100478ba);regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
for index,row in enumerate(m['records']):
 print('Checking',index,row['name'],flush=True)
 fn=getattr(lib,'tested_'+str(index));fn.argtypes=[ctypes.c_void_p];u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
 start=row['start'];code=bytes.fromhex(row['after_bytes'])
 regions=[(0x200000,65536),(0x220000,8192),(0x9a7000,4096),(0x9b1000,4096),(0x1046c000,4096)]
 pages={start&~4095,(start+len(code))&~4095}|{t&~4095 for t in row['targets']}
 for at in pages:u.mem_map(at,4096)
 for at,n in regions:u.mem_map(at,n)
 u.mem_write(start,code);hit=[]
 def stop(c,a,n,_):
  if a in row['targets']:hit.append(a);c.emu_stop()
 u.hook_add(unicorn.UC_HOOK_CODE,stop)
 for sample in range(256):
  for at,n in regions:
   data=rng.getrandbits(n*8).to_bytes(n,'little');u.mem_write(at,data);ctypes.memmove(base+at,data,n)
  state=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);values=[rng.getrandbits(32) for _ in range(8)]+[0x202|(sample&0xd5)];values[4]=0x208000+sample;values[5]=0x204000+sample;values[6]=0x220100;struct.pack_into('<9I',state,0,*values);struct.pack_into('<Q',state,112,base);expected=bytearray(state.raw)
  pointer=struct.pack('<I',0x221000);u.mem_write(0x2201c4,pointer);ctypes.memmove(base+0x2201c4,pointer,4)
  for address in (0x2211a5,0x1046cb8d,0x9b175d):
   byte=bytes([(1 if sample%2 else 0) if address==0x9b175d else (0 if sample%2 else 1)]);u.mem_write(address,byte);ctypes.memmove(base+address,byte,1)
  for r,v in zip(regs,values):u.reg_write(r,v)
  hit.clear();u.emu_start(start,0,count=64);assert len(hit)==1,(index,hit);struct.pack_into('<9I',expected,0,*[u.reg_read(r) for r in regs]);seen.value=0;fn(state);assert seen.value==hit[0] and state.raw==expected,(index,sample)
  for at,n in regions:assert ctypes.string_at(base+at,n)==bytes(u.mem_read(at,n)),(index,sample,hex(at))
(out/'test-result.json').write_text(json.dumps(dict(passed=True,cases=4608,call_tail_cases=1792,branch_tail_cases=256,whole_block_cases=2560,preparation_sha256=sha(out/'preparation.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Seven CALL tails and one x87 branch tail with unchanged prefixes, ten whole modified blocks vs independent x86. All 440 CPU bytes/all flags,64KiB stack and touched global pages, varied unaligned stack/frame addresses. Not gameplay proof.'),indent=2)+'\n');print('PASS: 4608 native/x86 comparisons')
