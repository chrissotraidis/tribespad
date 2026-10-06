#!/usr/bin/env python3
"""Differential MMX packed arithmetic: native ARM64 C vs x86 Unicorn."""
import ctypes,hashlib,json,pathlib,random,subprocess
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EFLAGS
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/mmx';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['clang','-arch','arm64','-O2','-std=c11','-Wall','-Wextra','-Werror','-dynamiclib',str(R/'port/core/mmx.c'),str(R/'port/llasm-support/tribes_mmx.c'),'-o',str(out/'libmmx.dylib')],check=True)
lib=ctypes.CDLL(str(out/'libmmx.dylib'));lib.tb_mmx_eval.argtypes=[ctypes.c_uint,ctypes.c_uint64,ctypes.c_uint64];lib.tb_mmx_eval.restype=ctypes.c_uint64
ops=['movq','paddb','paddw','psubw','pand','por','pxor','pmulhw','pmaddwd','punpcklbw','punpckhbw','punpcklwd','punpckldq','packuswb','packssdw','psraw','psrlw','psllw','psrld','pslld','psrlq','psllq']
codes=['6f','fc','fd','f9','db','eb','ef','e5','f5','60','68','61','62','67','6b','e1','d1','f1','d2','f2','d3','f3']
md=Cs(CS_ARCH_X86,CS_MODE_32);rng=random.Random(25034)
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x1000,0x2000)
# Load MM0/MM1 from guest data, execute one instruction, store MM0.
prefix=bytes.fromhex('0f6f05002000000f6f0d08200000');suffix=bytes.fromhex('0f7f05102000000f77')
edges=[0,1,0xffffffffffffffff,0x8000800080008000,0x7fff7fff7fff7fff,0x000100ff7f80ff00,0x8000000080000000,0x7fffffff7fffffff]
count=0;details={}
for op,(name,h) in enumerate(zip(ops,codes)):
 insn=bytes.fromhex('0f'+h+'c1');assert next(md.disasm(insn,0)).mnemonic==name
 pairs=[(a,b) for a in edges for b in edges]+[(rng.getrandbits(64),rng.getrandbits(64)) for _ in range(256)]
 if op>=15:pairs += [(a,b) for a in edges for b in [0,1,7,15,16,17,31,32,33,63,64,65,255,256,2**32,2**63]]
 for a,b in pairs:
  code=prefix+insn+suffix;u.mem_write(0x1000,code);u.mem_write(0x2000,a.to_bytes(8,'little')+b.to_bytes(8,'little'))
  flags=0x202|rng.choice([0,0x8d5,0x41,0x884]);u.reg_write(UC_X86_REG_EFLAGS,flags);u.emu_start(0x1000,0x1000+len(code))
  expected=int.from_bytes(u.mem_read(0x2010,8),'little');actual=lib.tb_mmx_eval(op,a,b)
  assert actual==expected,(name,hex(a),hex(b),hex(expected),hex(actual))
  assert u.reg_read(UC_X86_REG_EFLAGS)==flags,(name,'flags')
  count+=1
 details[name]=len(pairs)
result={'architecture':subprocess.check_output(['file',str(out/'libmmx.dylib')],text=True).strip(),'cases':count,'operations':details,'oracle':'Original x86 MMX instruction bytes in Unicorn 2.1.4','binary_sha256':hashlib.sha256((out/'libmmx.dylib').read_bytes()).hexdigest(),'pass':True,'limits':['No full x87 exception/tag validation yet','Whole engine has not run']}
(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
