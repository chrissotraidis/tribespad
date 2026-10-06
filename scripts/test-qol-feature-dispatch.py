#!/usr/bin/env python3
"""Execute original IFC22 CPU detector/dispatch with native CPUID contract."""
import ctypes,hashlib,json,pathlib,struct,subprocess
import capstone,pefile,unicorn,zlib
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/qol-feature-dispatch';out.mkdir(exist_ok=True);image=R/'ref/product/IFC22.dll';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();pe=pefile.PE(str(image));base=pe.OPTIONAL_HEADER.ImageBase;mapped=pe.get_memory_mapped_image();sources=[R/'port/core/guest_cpu.c',R/'port/core/guest_cpu.h',R/'port/llasm-support/llasm_cpu.h'];library=out/'cpu.dylib';subprocess.run(['clang','-dynamiclib','-O2','-Wall','-Wextra','-Werror',str(sources[0]),'-o',str(library)],check=True);native=ctypes.CDLL(str(library));native.x86_cpuid.argtypes=[ctypes.c_void_p];native.x86_cpuid.restype=None
regs=[X.UC_X86_REG_EAX,X.UC_X86_REG_ECX,X.UC_X86_REG_EDX,X.UC_X86_REG_EBX,X.UC_X86_REG_ESP,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI,X.UC_X86_REG_EFLAGS];decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);profiles=[]
for name in ['native','sse2','avx2','avx512-no-os-state','avx512']:
 u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(base,(len(mapped)+4095)&~4095);u.mem_write(base,mapped);u.mem_map(0x200000,0x20000);u.mem_map(0x300000,0x40000);u.mem_map(0x400000,0x1000);u.mem_write(0x400000,b'\x90');events=[];pcs=[];features=[];slot_writes=[];runs=[]
 def hook(uc,pc,size,data):
  raw=bytes(uc.mem_read(pc,size));pcs.append(pc)
  if raw==b'\x0f\xa2':
   leaf=uc.reg_read(X.UC_X86_REG_EAX);subleaf=uc.reg_read(X.UC_X86_REG_ECX)
   if name=='native':
    cpu=ctypes.create_string_buffer(440);values=[uc.reg_read(r) for r in regs];ctypes.memmove(cpu,struct.pack('<9I',*values),36);before=cpu.raw;native.x86_cpuid(cpu);after=struct.unpack('<9I',cpu.raw[:36]);assert cpu.raw[36:]==before[36:] and after[4:]==tuple(values[4:]);a,c,d,b=after[:4]
   elif leaf==0:a,b,c,d=7,0x756e6547,0x6c65746e,0x49656e69
   elif leaf==1:a,b,c,d=0x633,0,(0 if name=='sse2' else (1<<27)),(1<<26)
   elif leaf==7:a,b,c,d=0,(1<<5)|(1<<3),0,0
   else:a,b,c,d=0,0,0,0
   if name.startswith('avx512') and leaf==7:b|=(1<<16)|(1<<17)|(1<<30)|(1<<31)
   for r,v in [(X.UC_X86_REG_EAX,a),(X.UC_X86_REG_EBX,b),(X.UC_X86_REG_ECX,c),(X.UC_X86_REG_EDX,d)]:uc.reg_write(r,v)
   events.append({'pc':hex(pc),'leaf':leaf,'subleaf':subleaf,'eax':a,'ebx':b,'ecx':c,'edx':d,'source':'native x86_cpuid' if name=='native' else 'diagnostic CPUID fixture'});uc.reg_write(X.UC_X86_REG_EIP,pc+size)
  elif raw==b'\x0f\x01\xd0':
   assert name!='native';value=0xe6 if name=='avx512' else 6;uc.reg_write(X.UC_X86_REG_EAX,value);uc.reg_write(X.UC_X86_REG_EDX,0);events.append({'pc':hex(pc),'operation':'xgetbv','ecx':uc.reg_read(X.UC_X86_REG_ECX),'eax':value,'source':'diagnostic XCR0 fixture'});uc.reg_write(X.UC_X86_REG_EIP,pc+size)
  elif raw[:1] in (b'\xc4',b'\xc5',b'\x62'):
   ins=next(decoder.disasm(raw,pc,count=1));assert ins.mnemonic in ['les','lds','bound'],('unexpected SIMD execution',hex(pc),ins.mnemonic)
  if pc==0x100bdb5c:features.append(list(struct.unpack('<15I',uc.mem_read(uc.reg_read(X.UC_X86_REG_ESP)+0x38,60))))
 def write(uc,access,address,size,value,data):
  if 0x101d5478<=address<0x101d54b4:slot_writes.append({'slot':hex(address),'value':hex(value),'pc':hex(uc.reg_read(X.UC_X86_REG_EIP))})
 u.hook_add(unicorn.UC_HOOK_CODE,hook);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write)
 for iteration in range(2):
  sp=0x21f000;u.mem_write(sp,struct.pack('<I',0x400000));u.reg_write(X.UC_X86_REG_ESP,sp);u.reg_write(X.UC_X86_REG_EFLAGS,0x202);start=len(pcs);u.emu_start(0x100bdb40,0x400000,count=100000);assert u.reg_read(X.UC_X86_REG_EIP)==0x400000 and u.reg_read(X.UC_X86_REG_ESP)==sp+4
  runs.append({'instructions':len(pcs)-start,'slots':list(struct.unpack('<15I',u.mem_read(0x101d5478,60)))})
 assert runs[0]['slots']==runs[1]['slots'] and len(features)==2 and features[0]==features[1],(name,runs,features)
 expected={'native':(0x100c1c20,0x100c1bf0),'sse2':(0x100c1c20,0x100c1bf0),'avx2':(0x100c5ae0,0x100c5b00),'avx512-no-os-state':(0x100c5ae0,0x100c5b00),'avx512':(0x100c74e0,0x100c7500)}[name];assert tuple(runs[0]['slots'][1:3])==expected,(name,[hex(x) for x in runs[0]['slots']],features)
 if name=='native':assert features==[[0]*15]*2 and all(e.get('operation')!='xgetbv' for e in events)
 checksum_cases=[]
 if name=='native':
  for length in [0,1,15,16,31,32,63,64,65,255,256,5551,5552,5553,16384,65536]:
   for seed in [0,1,0xffff,0xffffffff,0x89abcdef]:
    data=bytes((j*17+length*3)&255 for j in range(length));source=0x300011;destination=0x320017;source_bytes=bytes([0xa6])*17+data+bytes([0xa6])*32;destination_bytes=bytes([0xb7])*(23+length+32);u.mem_write(0x300000,source_bytes);u.mem_write(0x320000,destination_bytes)
    for copy in [False,True]:
     if copy:u.mem_write(0x320000,destination_bytes)
     sp=0x21f000;args=[seed,destination,source,length] if copy else [seed,source,length];u.mem_write(sp,struct.pack('<'+str(len(args)+1)+'I',0x400000,*args));u.reg_write(X.UC_X86_REG_ESP,sp);u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
     preserved=[(r,0x13570000+j*0x10203) for j,r in enumerate([X.UC_X86_REG_EBX,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI])]
     for r,value in preserved:u.reg_write(r,value)
     start=len(pcs);u.emu_start(0x100bda70 if copy else 0x100bda60,0x400000,count=5000000);assert u.reg_read(X.UC_X86_REG_EIP)==0x400000 and u.reg_read(X.UC_X86_REG_ESP)==sp+4
     assert u.reg_read(X.UC_X86_REG_EAX)==zlib.adler32(data,seed),(length,seed,copy,hex(u.reg_read(X.UC_X86_REG_EAX)),hex(zlib.adler32(data,seed)))
     assert all(u.reg_read(r)==value for r,value in preserved)
     assert bytes(u.mem_read(0x300000,len(source_bytes)))==source_bytes
     wanted=destination_bytes if not copy else destination_bytes[:23]+data+destination_bytes[23+length:]
     assert bytes(u.mem_read(0x320000,len(wanted)))==wanted
     checksum_cases.append({'length':length,'seed':seed,'copy':copy,'instructions':len(pcs)-start,'result':u.reg_read(X.UC_X86_REG_EAX)})
  assert all(f==[0]*15 for f in features)
 profiles.append({'checksum_cases':checksum_cases,'name':name,'cpu_events':events,'features':features[0],'runs':runs,'slot_writes':slot_writes,'avx512_adler_selected':runs[0]['slots'][1]==0x100c74e0})
report={'pass':True,'image_sha256':sha(image),'script_sha256':sha(pathlib.Path(__file__)),'native_source_sha256':{str(p.relative_to(R)):sha(p) for p in sources},'native_binary_sha256':sha(library),'unicorn_version':unicorn.__version__,'profiles':profiles,'scope':'Original x86 feature detector and pointer publication only, current native CPUID C helper versus diagnostic SSE2/AVX2/AVX512 CPUID+XCR0 fixtures. Two initial publications/profile; native profile additionally executes160 checksum/checksumcopy calls through original lazy-dispatch entries against Python zlib and guardedmemory/callee-savedGPRs. Not native ARM dispatcher execution/full DLL initialization, complete indirect reachability proof or justification to omit arbitrary functions. No rendering/online/gameplay proof.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: original IFC22 feature dispatch in 5 profiles, native CPUID selects scalar Adler routines with160 checksum/copy cases; AVX512 selection requires OS state')
