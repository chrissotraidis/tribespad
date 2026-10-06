#!/usr/bin/env python3
"""Original CRC/version through actual SRW blocks and finite native VA dispatch."""
import argparse,ctypes as C,hashlib,importlib.util,json,pathlib,random,re,struct,subprocess,zlib
import pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args();work=a.lift.resolve();out=work/'va/tests';out.mkdir(parents=True,exist_ok=True)
identity=json.loads((work/'result.json').read_text());exe=work/'Tribes2.exe';assert hashlib.sha256(exe.read_bytes()).hexdigest()==identity['image_sha256']
blocks={}
for block in (work/'seg01_code.llinc').read_text().split('\nendp'):
 m=re.search(r'^proc (loc_[0-9A-F]+)\n',block,re.M)
 if m:blocks[m[1]]=block[m.start():]+'\nendp\n'
needed={'loc_6C32F0','loc_6949C0'};todo=list(needed)
while todo:
 name=todo.pop()
 for target in re.findall(r'^\s*(?:tcall|ctcallz|ctcallnz)\s+.*?\b(loc_[0-9A-F]+)\s*$',blocks[name],re.M):
  if target not in needed:assert target in blocks;needed.add(target);todo.append(target)
fixture='proc loc_18000000\nmov eax, loc_6949C0\ntcall eax\nendp\n'
code,stats=va.transform((''.join(blocks[n] for n in sorted(needed))+fixture).splitlines())
(out/'routines.llasm').write_text('include tribes_shift.llinc\ninclude llasm.llinc\nproc tribes_dispatch external\nfuncv tribes_set_dispatch_target $, target\n'+'\n'.join(code)+'\n')
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(out/'routines.llasm')],check=True,capture_output=True)
procs,removed=va.postprocess(out/'raw.ll',out/'routines.ll');entries=sorted((int(n[4:],16),n) for n in procs);(out/'dispatch.ll').write_text(va.dispatch_ir(entries))
subprocess.run(['clang','-arch','arm64','-O2','-Wno-override-module','-std=c11','-Wall','-Wextra','-Werror','-DPTROFS_64BIT','-dynamiclib',str(out/'routines.ll'),str(out/'dispatch.ll'),str(R/'port/core/guest_dispatch.c'),str(R/'port/core/shift.c'),str(R/'port/llasm-support/tribes_shift.c'),'-o',str(out/'libva.dylib')],check=True)
class CPU(C.Structure):
 _fields_=[('r',C.c_uint32*9),('top',C.c_uint32),('sw',C.c_uint32),('cw',C.c_uint32),('st',C.c_uint64*8),('offset',C.c_uint64),('result',C.c_int64),('bottom',C.c_void_p),('stacktop',C.c_void_p),('tag',C.c_uint32),('mmx',C.c_uint32),('exp',C.c_uint16*8),('extended',C.c_uint32),('dispatch',C.c_uint32),('xmm',(C.c_uint8*16)*8),('mxcsr',C.c_uint32),('sse_reserved',C.c_uint32),('ymm_hi',(C.c_uint8*16)*8)]
# Reserve a full guest window without committing its unused pages. All code is
# native dylib text; these original PE pages are data-only, never executable.
system=C.CDLL(None);system.mmap.argtypes=[C.c_void_p,C.c_size_t,C.c_int,C.c_int,C.c_int,C.c_int64];system.mmap.restype=C.c_void_p;system.mprotect.argtypes=[C.c_void_p,C.c_size_t,C.c_int];system.munmap.argtypes=[C.c_void_p,C.c_size_t]
base=system.mmap(None,1<<32,0,0x1002,-1,0);assert base not in (None,C.c_void_p(-1).value)
image=pefile.PE(str(exe)).get_memory_mapped_image();dataVA,stackVA,stop=0x2000000,0x3000000,va.HOST_RETURN
for address,size in [(0x400000,len(image)),(dataVA,65536),(stackVA,4096)]:assert system.mprotect(base+address,(size+16383)&~16383,3)==0
C.memmove(base+0x400000,image,len(image));lib=C.CDLL(str(out/'libva.dylib'));lib.tribes_enter.argtypes=[C.POINTER(CPU),C.c_uint32]
regs=[X.UC_X86_REG_EAX,X.UC_X86_REG_ECX,X.UC_X86_REG_EDX,X.UC_X86_REG_EBX,X.UC_X86_REG_ESP,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI]
rng=random.Random(0x25034);lengths=list(range(33))+[127,255,256,257,1024,4096,65535,65536]+[rng.randrange(8192) for _ in range(200)];cases=[(0x6c32f0,bytes(rng.getrandbits(8) for _ in range(n)),rng.getrandbits(32),False) for n in lengths]+[(0x6c32f0,b'123456789',0,False),(0x6c32f0,b'',0xaabbccdd,True),(0x6949c0,b'',0,False),(0x18000000,b'',0,False)]
checks=0
for entry,data,seed,null in cases:
 stack=bytearray(rng.getrandbits(8) for _ in range(4096));sp=stackVA+2048
 # Host entry supplies the sentinel; the original ABI's args follow it.
 struct.pack_into('<IIII',stack,2048,stop,seed,0 if null else dataVA,len(data));C.memmove(base+stackVA,bytes(stack),4096);C.memmove(base+dataVA,data or b'\0',max(1,len(data)))
 c=CPU();initial=[rng.getrandbits(32) for _ in range(8)];initial[4]=sp;c.r[:8]=initial;c.r[8]=2;c.r[4]=sp+4;c.offset=base;lib.tribes_enter(C.byref(c),entry)
 u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x400000,(len(image)+4095)&~4095);u.mem_write(0x400000,image);u.mem_map(dataVA,65536);u.mem_write(dataVA,data or b'\0');u.mem_map(stackVA,4096);u.mem_write(stackVA,bytes(stack));u.mem_map(stop,4096);u.mem_map(0x18000000,4096);u.mem_write(0x18000000,b'\xb8\xc0\x49\x69\x00\xff\xe0')
 for reg,v in zip(regs,initial):u.reg_write(reg,v)
 u.reg_write(X.UC_X86_REG_EFLAGS,2);u.emu_start(entry,stop,count=max(200,100*len(data)))
 expected=25034 if entry in [0x6949c0,0x18000000] else (0 if null else zlib.crc32(data,seed))
 assert list(c.r[:8])==[u.reg_read(reg) for reg in regs],(entry,len(data),'registers')
 assert (c.r[8]&0xcd5)==(u.reg_read(X.UC_X86_REG_EFLAGS)&0xcd5),'flags'
 assert C.string_at(base+stackVA,4096)==bytes(u.mem_read(stackVA,4096)),'stack and original return addresses'
 assert C.string_at(base+dataVA,max(1,len(data)))==(data or b'\0') and c.r[0]==expected
 checks+=1
assert system.munmap(base,1<<32)==0
report={'pass':True,'cases':checks,'actual_srw_procedures':len(procs)-1,'synthetic_indirect_jump_regression_cases':1,'original_game_cases':checks-1,'stats':stats,'image_sha256':identity['image_sha256'],'native_library_sha256':hashlib.sha256((out/'libva.dylib').read_bytes()).hexdigest(),'checks':['original guest code/data VAs','native finite dispatch and host return sentinel','all GPRs and defined flags against original x86','complete stack bytes and preserved input','independent CRC and game version'],'whole_game_entered':False}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
