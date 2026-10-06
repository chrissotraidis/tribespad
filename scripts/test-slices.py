#!/usr/bin/env python3
"""Compare AOT ARM64 routines with original x86 execution, including ABI and memory effects."""
import subprocess
import ctypes as C,hashlib,json,pathlib,random,struct,zlib
import pefile,unicorn
from unicorn import x86_const as X
ROOT=pathlib.Path(__file__).resolve().parents[1]
class Region(C.Structure):_fields_=[('base',C.c_uint32),('size',C.c_uint32),('host',C.POINTER(C.c_uint8))]
class State(C.Structure):_fields_=[('r',C.c_uint32*8),('pc',C.c_uint32),('cf',C.c_uint32),('zf',C.c_uint32),('sf',C.c_uint32),('of',C.c_uint32),('pf',C.c_uint32),('regions',Region*3)]
REGS=[X.UC_X86_REG_EAX,X.UC_X86_REG_ECX,X.UC_X86_REG_EDX,X.UC_X86_REG_EBX,X.UC_X86_REG_ESP,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI]
def main():
 source=ROOT/'ref/control/Tribes2.exe';img=pefile.PE(str(source)).get_memory_mapped_image();base=0x400000;dataVA=0x2000000;stackVA=0x3000000;stop=0x4000000
 identity=json.loads((ROOT/'generated/slices/identity.json').read_text())
 if hashlib.sha256(source.read_bytes()).hexdigest()!=identity['image_sha256']:raise SystemExit('Input changed')
 arch=subprocess.check_output(['lipo','-archs',str(ROOT/'generated/slices/libtribes_slices.dylib')],text=True).strip()
 if arch!='arm64':raise SystemExit('Unexpected native architecture: '+arch)
 lib=C.CDLL(str(ROOT/'generated/slices/libtribes_slices.dylib'))
 image=(C.c_uint8*len(img)).from_buffer_copy(img);rng=random.Random(0x54524942);results=[]
 lengths=list(range(33))+[127,255,256,257,1024,4096,65535,65536]+[rng.randrange(8192) for _ in range(200)]
 cases=[('tb_crc32',0x6c32f0,bytes(rng.getrandbits(8) for _ in range(n)),rng.getrandbits(32),False) for n in lengths]
 cases+=[('tb_crc32',0x6c32f0,b'123456789',0,False),('tb_crc32',0x6c32f0,b'',0xaabbccdd,True),('tb_game_version',0x6949c0,b'',0,False)]
 for name,entry,data,seed,null in cases:
  stack=bytearray(rng.getrandbits(8) for _ in range(4096));sp=stackVA+2048;struct.pack_into('<IIII',stack,2048,stop,seed,0 if null else dataVA,len(data))
  stackbuf=(C.c_uint8*len(stack)).from_buffer_copy(stack);databuf=(C.c_uint8*max(1,len(data))).from_buffer_copy(data or b'\0');s=State();initial=[rng.getrandbits(32) for _ in range(8)];initial[4]=sp;s.r[:]=initial
  s.regions[:]=[Region(base,len(img),image),Region(dataVA,len(databuf),databuf),Region(stackVA,len(stack),stackbuf)]
  fn=getattr(lib,name);fn.argtypes=[C.POINTER(State)];fn.restype=None;fn(C.byref(s))
  u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(base,(len(img)+4095)&~4095);u.mem_write(base,img);u.mem_map(dataVA,(len(databuf)+4095)&~4095);u.mem_write(dataVA,data or b'\0');u.mem_map(stackVA,4096);u.mem_write(stackVA,bytes(stack));u.mem_map(stop,4096)
  for reg,value in zip(REGS,initial):u.reg_write(reg,value)
  u.reg_write(X.UC_X86_REG_EFLAGS,2);u.emu_start(entry,stop,count=max(200,100*len(data)))
  oracle=[u.reg_read(reg) for reg in REGS];flags=u.reg_read(X.UC_X86_REG_EFLAGS)
  actualflags=[s.cf,s.pf,s.zf,s.sf,s.of];oracleflags=[(flags>>i)&1 for i in [0,2,6,7,11]]
  # Version getter preserves flags; initialized zero matches EFLAGS=2.
  expected=25034 if name=='tb_game_version' else (0 if null else zlib.crc32(data,seed))
  ok=list(s.r)==oracle and s.pc==u.reg_read(X.UC_X86_REG_EIP) and bytes(stackbuf)==bytes(u.mem_read(stackVA,4096)) and bytes(databuf)==(data or b'\0') and s.r[0]==expected and actualflags==oracleflags
  results.append({'routine':name,'length':len(data),'null':null,'seed':seed,'native_eax':s.r[0],'oracle_eax':oracle[0],'ok':ok})
  if not ok:print('FAIL',results[-1],list(s.r),oracle,actualflags,oracleflags)
 report={'architecture':arch,'native_library_sha256':hashlib.sha256((ROOT/'generated/slices/libtribes_slices.dylib').read_bytes()).hexdigest(),'cases':len(results),'failures':sum(not r['ok'] for r in results),'result':'PASS' if all(r['ok'] for r in results) else 'FAIL','identity':identity,'checks':['all integer registers','callee-save registers','stack bytes and return address','input preservation','CF/PF/ZF/SF/OF','CRC32 against zlib','build 25034'],'results':results}
 (ROOT/'generated/slices/results.json').write_text(json.dumps(report,indent=2)+'\n');print(report['result'],report['cases'],'original-x86 versus native-ARM64 cases;',report['failures'],'failures');raise SystemExit(bool(report['failures']))
if __name__=='__main__':main()
