#!/usr/bin/env python3
"""Finite CRT query: real x86 environment pair plus whole original IA32 routine."""
import ctypes, hashlib, json, os, pathlib, random, re, struct, subprocess
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import *
from checked_support import compile_support
from guard_checked_link import guard_link
from fp_control_query_inputs import translate, verify, START, END, PAIR, RESTORE, MXCSR
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/fp-control-query';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
image=R/'ref/qol/IFC22.dll';proof=verify(image)
original=(R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll').read_text()
parts=[m.group() for m in re.finditer(r'define (?:hidden|protected) fastcc void @loc_([0-9A-F]+).*?\n}',original,re.S) if START<=int(m[1],16)<END]
assert len(parts)==32
fixture=re.search(r'^%_cpu = type .*$',original,re.M)[0]+'\n'+'\n'.join(parts)+'\n'
for name in ['tb_shift_reg','tb_sse_binary','tb_sse_store','tb_sse_cmov','tribes_set_dispatch_target']:
 fixture+=re.search(r'^declare[^\n]*@'+name+r'\([^\n]*$',original,re.M)[0]+'\n'
fixture+='define hidden fastcc void @tribes_dispatch(%_cpu* %cpu) { ret void }\ndefine void @query(%_cpu* %cpu) { call fastcc void @loc_1011D750(%_cpu* %cpu)\n ret void }\n'
ir=out/'query.ll';ir.write_text(translate(fixture)[0])
rejected=0
for pc in [PAIR,RESTORE,MXCSR]:
 needle='call void @halopad_trap_unimplemented(i32 %d) nounwind'%pc
 for text in [fixture.replace(needle,''),fixture+needle]:
  try:translate(text)
  except AssertionError:rejected+=1
  else:raise AssertionError('accepted missing/duplicated site')
try:translate(fixture.replace(';call halopad_trap_unimplemented 0x1011d771','store i32 0, i32* null'))
except AssertionError:rejected+=1
else:raise AssertionError('accepted intervening operation')
helper=R/'port/llasm-support/tribes_fp_query.c'
stub=out/'return.c';stub.write_text('#include "'+str(R/'port/llasm-support/llasm_cpu.h')+'"\nvoid tribes_set_dispatch_target(CPU,uint32_t target){cpu->_dispatch_target=target;}\n')
math=sse_inputs('macos');sources=[helper,ir,R/'port/llasm-support/tribes_sse.c',R/'port/llasm-support/tribes_shift.c',R/'port/core/shift.c',stub]+list(map(pathlib.Path,math['sources']))
objects,_=compile_support(sources,out/'checked',['-DPTROFS_64BIT',*math['compile_flags']]);meta=json.loads((out/'checked/result.json').read_text())
entries=[dict(object=p,object_sha256=sha(pathlib.Path(p)),bitcode=str(pathlib.Path(p).with_suffix('.bc')),bitcode_sha256=meta['objects'][pathlib.Path(p).stem]['bitcode_sha256']) for p in objects]
objects,_=guard_link(entries,out/'guarded')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();flags=['clang','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wno-override-module']
common=[str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c')]
libs={}
for mode in ['normal','checked']:
 lib=out/(mode+'.dylib');inputs=list(map(str,sources)) if mode=='normal' else objects
 subprocess.run(flags+['-dynamiclib',*math['compile_flags'],*inputs,*common,*math['libraries'],'-o',str(lib)],check=True)
 libs[mode]=ctypes.CDLL(str(lib))
oracle=out/'x86-oracle';subprocess.run(flags+['-arch','x86_64',str(R/'tests/fp_control_query_oracle.c'),'-o',str(oracle)],check=True)
rng=random.Random(25034);states=[]
values=[(0,0),(1,0),(0x8000000000000000,0x3fff),(0x8000000000000000,0xbfff),(0x8000000000000000,0x7fff),(0xc000000000000123,0x7fff)]
for row in range(512):
 cw=0x7f|((row%4)<<10)|(((row//4)%4)<<8)
 sw=rng.randrange(0x8000)&~0x80;tag=0
 for i in range(8):
  sig,exp=values[(row+i)%len(values)];t=1 if not sig and not exp else 2 if not exp or exp==0x7fff else 0
  if rng.randrange(2):t=3
  tag|=t<<(2*((i+(sw>>11&7))%8))
 state=bytearray(struct.pack('<7I',cw,sw,tag,0,0,0,0))
 for i in range(8):state+=struct.pack('<QH',*values[(row+i)%len(values)])
 states.append(state)
run=subprocess.run(['arch','-x86_64',str(oracle)],input=b''.join(states),capture_output=True,check=True)
assert len(run.stdout)==len(states)*84;(out/'reference.bin').write_bytes(run.stdout)
native=ctypes.CDLL(None);native.mmap.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_long];native.mmap.restype=ctypes.c_void_p
native.mprotect.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int]
base=native.mmap(None,0x100000000,0,0x1002,-1,0);assert base not in [0,ctypes.c_void_p(-1).value]
page=os.sysconf('SC_PAGE_SIZE');assert not native.mprotect(base,2*page,3)
page_global=0x1046bfd0//page*page;assert not native.mprotect(base+page_global,page,3)
capture=(R/'generated/integration-review/apple-render-dimensions/scene-vertex-vector-12388/cpu.bin').read_bytes();assert len(capture)==440
report={};cpu=ctypes.create_string_buffer(440)
for mode,lib in libs.items():
 lib.tribes_memory_checks_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];assert lib.tribes_memory_checks_configure(base,0x100000000)
 lib.tribes_memory_external_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint64];lib.tribes_memory_external_configure(base,0x100000000)
 targets=(ctypes.c_void_p*1)(ctypes.cast(lib.query,ctypes.c_void_p).value);lib.tribes_memory_register_targets.argtypes=[ctypes.c_void_p,ctypes.c_uint64];lib.tribes_memory_register_targets(targets,1);lib.tribes_memory_external_enable()
 lib.tribes_memory_permissions_set.argtypes=[ctypes.c_uint32,ctypes.c_uint64,ctypes.c_uint32]
 assert lib.tribes_memory_permissions_set(0,0x100000000,0)
 assert lib.tribes_memory_permissions_set(0,8192,3);assert lib.tribes_memory_permissions_set(page_global,page,3)
 for name in ['tb_fp_control_query_pair','tb_fp_control_query_mxcsr','query']:getattr(lib,name).argtypes=[ctypes.c_void_p]
 for row,state in enumerate(states):
  before,saved,after=[run.stdout[row*84+k*28:row*84+(k+1)*28] for k in range(3)]
  cw,sw,tag=struct.unpack_from('<3I',before);cw&=65535;sw&=65535;tag&=65535;top=sw>>11&7
  current=bytearray(capture);struct.pack_into('<I',current,20,4096+(row%2)*7)
  struct.pack_into('<3I',current,36,top,sw&~0x3800,cw);struct.pack_into('<Q',current,112,base);struct.pack_into('<2I',current,144,tag,0);struct.pack_into('<I',current,168,1)
  for i in range(8):
   sig,exp=struct.unpack_from('<QH',state,28+i*10);physical=(top+i)%8
   struct.pack_into('<Q',current,48+physical*8,sig);struct.pack_into('<H',current,152+physical*2,exp)
  expected=bytearray(current);acw,asw,atag=[v&65535 for v in struct.unpack_from('<3I',after)]
  struct.pack_into('<3I',expected,36,asw>>11&7,asw&~0x3800,acw);struct.pack_into('<I',expected,144,atag)
  ctypes.memmove(cpu,bytes(current),440);ctypes.memset(base,0xa5,8192);lib.tb_fp_control_query_pair(cpu)
  assert cpu.raw==bytes(expected),(mode,row,'CPU',[(i,cpu.raw[i],expected[i]) for i in range(440) if cpu.raw[i]!=expected[i]][:10])
  address=struct.unpack_from('<I',current,20)[0]-32
  assert ctypes.string_at(base+address,2)==saved[:2]
  assert ctypes.string_at(base+address+2,26)==b'\xa5'*26
 # Exact original control-query body, independently executed as IA32.
 uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0,0x10000);uc.mem_map(START&~4095,4096);uc.mem_map(0x1046b000,4096)
 pe=pefile.PE(str(image));uc.mem_write(START,pe.get_data(START-0x10000000,END-START))
 registers=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS]
 for row in range(1024):
  current=bytearray(capture);cw=0x40|(row&63)|(((row//64)%4)<<8)|(((row//256)%4)<<10)
  mxcsr=rng.randrange(65536);sse=row%3-1
  struct.pack_into('<Q',current,112,base);struct.pack_into('<3I',current,36,0,0,cw);struct.pack_into('<I',current,144,65535);struct.pack_into('<I',current,304,mxcsr)
  struct.pack_into('<I',current,16,0x1800);struct.pack_into('<I',current,32,0x202)
  initial=b'\xa5'*0x1800+struct.pack('<I',0x800)+b'\xa5'*0x7fc
  ctypes.memmove(base,initial,len(initial));ctypes.memmove(base+0x1046bfd0,struct.pack('<i',sse),4);ctypes.memmove(cpu,bytes(current),440)
  uc.mem_write(0,initial);uc.mem_write(0x1046bfd0,struct.pack('<i',sse))
  for reg,value in zip(registers,struct.unpack_from('<9I',current)):uc.reg_write(reg,value)
  uc.reg_write(UC_X86_REG_FPCW,cw);uc.reg_write(UC_X86_REG_FPSW,0);uc.reg_write(UC_X86_REG_FPTAG,65535);uc.reg_write(UC_X86_REG_MXCSR,mxcsr)
  uc.emu_start(START,0x800,count=1000);assert uc.reg_read(UC_X86_REG_EIP)==0x800
  lib.query(cpu)
  actual=struct.unpack_from('<9I',cpu.raw);reference=tuple(uc.reg_read(r) for r in registers)
  assert actual==reference,(mode,row,actual,reference)
  assert struct.unpack_from('<I',cpu.raw,172)[0]==0x800
  assert cpu.raw[36:172]==bytes(current[36:172])
  assert cpu.raw[192:]==bytes(current[192:]) # only XORPS xmm0 changes vector state
  assert cpu.raw[176:192]==bytes(16)
 # Whole-range permission checks, including otherwise dead environment bytes.
 faults=[]
 for kind in ['readonly','writeonly','tail','wrap','badtop']:
  current=bytearray(capture);struct.pack_into('<Q',current,112,base);struct.pack_into('<I',current,20,4112 if kind=='tail' else 16 if kind=='wrap' else 4096)
  if kind=='badtop':struct.pack_into('<I',current,36,8)
  ctypes.memmove(cpu,bytes(current),440)
  assert lib.tribes_memory_permissions_set(0,8192,3)
  if kind in ['readonly','writeonly']:assert lib.tribes_memory_permissions_set(0,4096,1 if kind=='readonly' else 2)
  if kind=='tail':assert lib.tribes_memory_permissions_set(4096,4096,0)
  child=os.fork()
  if not child:
   lib.tb_fp_control_query_pair(cpu);os._exit(0)
  _,status=os.waitpid(child,0);assert os.WIFSIGNALED(status),(mode,kind,status);faults.append(kind)
 assert lib.tribes_memory_permissions_set(0,8192,3)
 report[mode]=dict(pair_x86_cases=len(states),original_routine_ia32_cases=1024,guard_stops=faults)
 print('PASS',mode,report[mode],flush=True)
report.update(passed=True,proof=proof,rejected_translations=rejected,sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/fp_control_query_inputs.py',helper,R/'tests/fp_control_query_oracle.c',R/'port/llasm-support/llasm_cpu.h',image]},scope='Finite pair optimization only; dead private-frame environment bytes intentionally not materialized. General FNSTENV/FLDENV remain unsupported. Mac component tests; original app acceptance separate.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
