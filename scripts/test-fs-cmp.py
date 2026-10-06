#!/usr/bin/env python3
"""Compare strict FS CMP lowering with original x86 and the real native TEB reader."""
import ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import pefile,unicorn
from unicorn import x86_const as X
from sse_instruction_fixture import fixture
from guest_teb_contract import configure_sparse_teb
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/fs-compare';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
original=R/'ref/product/IFC22.dll';assert pefile.PE(str(original)).get_data(0xb732c,7)==bytes.fromhex('643b0518000000')
records=[dict(bytes_hex=(bytes([0x64,0x3b,5|(reg<<3)])+struct.pack('<I',offset)).hex(),reg=reg,offset=offset) for offset in [0x18,0,4,8,0x20,0x24,0x2c,0x30,0xbf4] for reg in range(8)]
run=fixture(out/'fixture',records,original,srw);assert run.returncode==0,run.stderr
negative=[]
for name,raw in [('address16','67643b061800'),('operand16','66643b0518000000'),('lock','f0643b0518000000'),('rep','f3643b0518000000'),('repne','f2643b0518000000'),('regaddress','643b00'),('reverse','64390518000000')]:
 run=fixture(out/('negative-'+name),[dict(bytes_hex=raw)],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append(name)
s=(out/'fixture/seg01_code.llinc').read_text();matches=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',s,re.M|re.S));assert len(matches)==len(records) and 'halopad_trap_unimplemented' not in s
source=out/'bodies.llasm';source.write_text('include llasm.llinc\n'+''.join('proc compare_%s public\n%sendp\n'%(m[1],m[2].split(';ret\n')[0]) for m in matches))
ir=out/'bodies.ll';subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(ir),str(source)],check=True)
bridge=out/'exports.ll';bridge.write_text('\n'.join('declare hidden fastcc void @compare_%d(ptr)\ndefine void @exported_compare_%d(ptr %%cpu) { call fastcc void @compare_%d(ptr %%cpu)\n ret void\n}'%(i,i,i) for i in range(len(records))))
libpath=out/'native.dylib';subprocess.run(['clang','-arch','arm64','-O2','-Wno-override-module','-dynamiclib',str(ir),str(bridge),str(R/'port/core/guest_teb.c'),str(R/'port/core/guest_heap.c'),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));lib.tribes_teb_configure.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*6
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x10000,4096);u.mem_map(0x3c0000,0x20000)
teb=0x3d0000;mem=ctypes.create_string_buffer(4096);base=ctypes.addressof(mem)-teb
assert lib.tribes_teb_configure(base,teb,0x300000,0x100000,1234,2001,0x3e0000)==1
configure_sparse_teb(u,teb,0x3c0000,0x300000,0x100000,1234,2001,0x3e0000)
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];rng=random.Random(0x100b732c);count=0
for i,r in enumerate(records):
 fn=getattr(lib,'exported_compare_%d'%i);fn.argtypes=[ctypes.c_void_p];raw=bytes.fromhex(r['bytes_hex']);u.mem_write(0x10000,raw)
 for sample in range(128):
  right=teb if r['offset']==0x18 else rng.getrandbits(32)
  struct.pack_into('<I',mem,r['offset'],right);u.mem_write(teb,mem.raw)
  values=[rng.getrandbits(32) for _ in range(8)]+[0x202|sum(bit for j,bit in enumerate([1,4,16,64,128,2048]) if sample&(1<<j))]
  edges=[right,(right-1)&0xffffffff,(right+1)&0xffffffff,0,0xffffffff,0x80000000,0x7fffffff]
  values[r['reg']]=edges[sample%len(edges)] if sample<64 else rng.getrandbits(32)
  cpu=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);expected=bytearray(cpu.raw);before=mem.raw
  for reg,v in zip(regs,values):u.reg_write(reg,v)
  u.emu_start(0x10000,0x10000+len(raw),count=1);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);fn(cpu)
  assert cpu.raw==expected,(i,sample,[(j,cpu.raw[j],expected[j]) for j in range(440) if cpu.raw[j]!=expected[j]])
  assert mem.raw==before==bytes(u.mem_read(teb,4096));count+=1
lib.tribes_teb_clear()
report=dict(passed=True,x86_cases=count,blocks=len(records),negative=negative,srw_sha256=sha(srw),patch_sha256=build['patch_sha256'],original_sha256=sha(original),sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/patches/srw-tribes.patch',R/'port/core/guest_teb.c',R/'port/core/guest_heap.c']},scope='All eight r32 destinations, nine declared FS fields, original site, real native sparse TEB reads; all 440 CPU bytes and TEB memory checked against x86. No whole-game or mobile acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',count,'original-x86 / native FS compare cases')
