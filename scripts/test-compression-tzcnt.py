#!/usr/bin/env python3
"""Execute the original compression TZCNT translations against independent x86."""
import ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import capstone,pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS
from checked_support import compile_support
from guard_checked_link import guard_link
from patch_runtime_inputs import COMPRESSION_TZCNT_SITES,compression_tzcnt_body,translate_compression_tzcnt
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/compression-tzcnt';out.mkdir(parents=True,exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();(out/'result.json').write_text(json.dumps(dict(passed=False,state='running'))+'\n')
image=R/'ref/product/IFC22.dll';pe=pefile.PE(str(image));decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
names=['eax','ecx','edx','ebx','esp','ebp','esi','edi'];records=[]
fixture='%_cpu = type opaque\n'+''.join('define void @tz_%d(%%_cpu* %%cpu) {\n call void @halopad_trap_unimplemented(i32 %d) nounwind\n ret void\n}\n'%(i,row[0]) for i,row in enumerate(COMPRESSION_TZCNT_SITES))
first='call void @halopad_trap_unimplemented(i32 %d) nounwind'%COMPRESSION_TZCNT_SITES[0][0]
for bad in [fixture.replace(first,''),fixture+first,fixture+'\n; @tb_tzcnt32']:
 try:translate_compression_tzcnt(bad,image)
 except AssertionError:pass
 else:raise AssertionError('accepted changed/missing/duplicated instruction contract')
changed=out/'changed.dll';data=bytearray(image.read_bytes());data[-1]^=1;changed.write_bytes(data)
try:
 try:translate_compression_tzcnt(fixture,changed)
 except AssertionError:pass
 else:raise AssertionError('accepted changed image')
finally:changed.unlink()
source=out/'bodies.ll';text=translate_compression_tzcnt(fixture,image)
for pc,raw,destination,origin in COMPRESSION_TZCNT_SITES:
 ins=next(decoder.disasm(pe.get_data(pc-0x10000000,4),pc));assert ins.mnemonic=='tzcnt' and len(ins.operands)==2 and all(op.type==capstone.x86.X86_OP_REG and op.size==4 for op in ins.operands)
 assert ins.reg_name(ins.operands[0].reg)==names[destination] and ins.reg_name(ins.operands[1].reg)==names[origin]
 records.append(dict(pc=hex(pc),bytes_hex=raw,destination=destination,source=origin))
for destination in range(8):
 for origin in range(8):
  i=len(records);raw=bytes([0xf3,0x0f,0xbc,0xc0|(destination<<3)|origin]);ins=next(decoder.disasm(raw,0x10000));assert ins.mnemonic=='tzcnt'
  text+='define void @tz_%d(%%_cpu* %%cpu) {\n%s\n ret void\n}\n'%(i,compression_tzcnt_body(destination,origin,'alias_%d'%i));records.append(dict(pc=None,bytes_hex=raw.hex(),destination=destination,source=origin))
source.write_text(text)
# Verify only the finite instruction bodies change in the full original module.
module=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll';before=module.read_text();after=translate_compression_tzcnt(before,image);pattern=r'^define hidden fastcc void @(loc_[0-9A-F]+)\([^\n]*\n.*?^}\n'
a={m[1]:m[0] for m in re.finditer(pattern,before,re.M|re.S)};b={m[1]:m[0] for m in re.finditer(pattern,after,re.M|re.S)};assert a.keys()==b.keys()
expected={name for name,body in a.items() if any('call void @halopad_trap_unimplemented(i32 %d)'%row[0] in body for row in COMPRESSION_TZCNT_SITES)};changed_blocks=[name for name in a if a[name]!=b[name]];assert set(changed_blocks)==expected
assert after.count('call void @halopad_trap_unimplemented(')==before.count('call void @halopad_trap_unimplemented(')-len(COMPRESSION_TZCNT_SITES)
objects,_=compile_support([R/'port/llasm-support/tribes_scan.c',source],out/'checked',['-DPTROFS_64BIT']);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,_=guard_link(entries,out/'guarded');sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];report=dict(passed=False,original_sites=records[:16],changed_original_blocks=changed_blocks,source_rejections=4)
for mode in ['normal','checked']:
 library=out/(mode+'.dylib');inputs=objects if mode=='checked' else [str(source),str(R/'port/llasm-support/tribes_scan.c')]
 subprocess.run(['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wno-override-module','-dynamiclib',*inputs,*map(str,guards),'-o',str(library)],check=True)
 lib=ctypes.CDLL(str(library))
 if mode=='checked':
  # These register-only instructions use a private host CPU. Enable guards
  # for a separate guest window; no guest memory is read by this fixture.
  lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(0x7000000000,0x100000000)
  lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(0x7000000000,0x100000000);lib.tribes_memory_external_enable()
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,4096);registers=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS]
 rng=random.Random(25034);patterns=[0,1,0xffffffff,0x80000000,0x7fffffff,0x4000000]+[1<<i for i in range(32)]+[rng.getrandbits(32) for _ in range(64)];cases=zero_cases=0
 for i,row in enumerate(records):
  function=getattr(lib,'tz_'+str(i));function.argtypes=[ctypes.c_void_p];u.mem_write(0x10000,bytes.fromhex(row['bytes_hex']))
  for value in patterns:
   for flags in [0x202,0x243,0xad7,0xa96]:
    cpu=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440);values=[rng.getrandbits(32) for _ in range(8)]+[flags];values[row['source']]=value;struct.pack_into('<9I',cpu,0,*values);saved=cpu.raw
    for reg,v in zip(registers,values):u.reg_write(reg,v)
    u.emu_start(0x10000,0x10004,count=1);expected=[u.reg_read(reg) for reg in registers];function(cpu);actual=struct.unpack_from('<9I',cpu)
    assert list(actual[:8])==expected[:8] and (actual[8]&0x41)==(expected[8]&0x41),(mode,i,value,actual,expected)
    assert (actual[8]&~0x41)==(flags&~0x41) and cpu.raw[36:]==saved[36:]
    if not value:assert actual[row['destination']]==32 and actual[8]&1 and not(actual[8]&0x40);zero_cases+=1
    cases+=1
 report[mode]=dict(passed=True,cases=cases,zero_input_cases=zero_cases,library_sha256=sha(library));print('PASS:',mode,cases,'emitted ARM64/independent x86 cases',flush=True)
(out/'variants.inc').write_text('typedef struct {unsigned destination,source;} variant;\n'+''.join('extern void tz_%d(_cpu *);\n'%i for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('tz_%d'%i for i in range(len(records)))+'};\nstatic const variant variants[]={'+','.join('{%d,%d}'%(row['destination'],row['source']) for row in records)+'};\n')
test=R/'tests/compression_tzcnt_threads_test.c';exe=out/'tsan';subprocess.run(['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O1','-g','-fsanitize=thread','-Wall','-Wextra','-Werror','-DPTROFS_64BIT','-Wno-override-module','-I',str(out),str(test),str(source),str(R/'port/llasm-support/tribes_scan.c'),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/'tsan.log').write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);report['tsan']=dict(passed=True,binary_sha256=sha(exe),output=run.stdout);print(run.stdout.strip(),flush=True)
report.update(passed=True,checked_ir_sha256=sha(source),original_module_sha256=sha(module),translated_module_sha256=hashlib.sha256(after.encode()).hexdigest(),sources_sha256={str(p.relative_to(R)):sha(p) for p in [R/'port/llasm-support/tribes_scan.c',R/'scripts/patch_runtime_inputs.py',pathlib.Path(__file__).resolve(),test,R/'port/llasm-support/llasm_cpu.h']},scope='Sixteen exact original TZCNT32 register instructions and all64 GPR aliases, zero/singleton/random inputs; full CPU preservation, defined CF/ZF against independent Unicorn x86, undefined flags retained by policy, and TSan concurrency. Source/image gates and finite original-module changes verified. No general BMI, memory/16-bit TZCNT or menu/map/gameplay claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
