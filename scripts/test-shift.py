#!/usr/bin/env python3
"""Native packed result/flags vs x86, all CL counts and supported widths."""
import ctypes,json,pathlib,random,subprocess,hashlib
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from unicorn import *
from unicorn.x86_const import *
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/shift';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['clang','-arch','arm64','-O2','-std=c11','-Wall','-Wextra','-Werror','-dynamiclib',str(R/'port/core/shift.c'),str(R/'port/llasm-support/tribes_shift.c'),'-o',str(out/'libshift.dylib')],check=True)
class Result(ctypes.Structure):_fields_=[('value',ctypes.c_uint32),('flags',ctypes.c_uint32)]
lib=ctypes.CDLL(str(out/'libshift.dylib'));lib.tb_shift.argtypes=[ctypes.c_uint,ctypes.c_uint,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint,ctypes.c_uint32];lib.tb_shift.restype=Result
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x1000,0x1000);rng=random.Random(25034);cases={};total=0
for op,name in list(enumerate(['shl','shr','sar','rol','shrd']))+[(6,'ror')]:
 for width in ([32] if op==4 else [8,16,32]):
  modrm=0xc8 if op==6 else [0xe0,0xe8,0xf8,0xc0][op] if op<4 else 0xd0
  code=bytes([0xd2 if width==8 else 0xd3,modrm]) if op<4 or op==6 else bytes.fromhex('0fadd0')
  if width==16:code=b'\x66'+code
  decoded=next(Cs(CS_ARCH_X86,CS_MODE_32).disasm(code,0));assert decoded.mnemonic==name and decoded.op_str.endswith(', cl') and decoded.size==len(code)
  u.mem_write(0x1000,code);u.ctl_remove_cache(0x1000,0x1100);mask=(1<<width)-1
  values=[0,1,mask,1<<(width-1),(1<<(width-1))-1,mask-1,0x55&mask,0xaa&mask]+[rng.getrandbits(width) for _ in range(32)]
  for a in values:
   for n in range(256):
    flags=0x202|rng.choice([0,0x8d5,0x41,0x884]);source=rng.getrandbits(32);u.reg_write(UC_X86_REG_EAX,a);u.reg_write(UC_X86_REG_EDX,source);u.reg_write(UC_X86_REG_ECX,n);u.reg_write(UC_X86_REG_EFLAGS,flags);u.emu_start(0x1000,0x1000+len(code),count=1)
    expected=u.reg_read(UC_X86_REG_EAX)&mask;ef=u.reg_read(UC_X86_REG_EFLAGS);actual=lib.tb_shift(op,width,a,source,n,flags)
    assert actual.value==expected,(name,width,hex(a),n,'value',hex(actual.value),hex(expected))
    c=n&31;defined=0xffffffff
    if c:
     if op in [3,6]:
      if c!=1:defined&=~0x800
     else:
      defined&=~0x10 # AF undefined for a nonzero shift.
      if c!=1:defined&=~0x800
      if op in [0,1] and c>=width:defined&=~1
    assert (actual.flags&defined)==(ef&defined),(name,width,hex(a),n,'flags',hex(actual.flags),hex(ef),hex(defined))
    total+=1
  cases[f'{name}/{width}']=len(values)*256
result={'pass':True,'cases':total,'families':cases,'oracle':'x86 instructions in Unicorn 2.1.4','undefined_bits_excluded':['Nonzero shift AF','Multi-bit shift/rotate OF','SHL/SHR CF for counts >= operand width'],'library_sha256':hashlib.sha256((out/'libshift.dylib').read_bytes()).hexdigest()};(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
