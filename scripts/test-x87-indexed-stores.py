#!/usr/bin/env python3
"""Execute freshly lowered x87 stores with scaled/aliased effective addresses."""
import ctypes,hashlib,json,pathlib,re,struct,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/x87-indexed-stores';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text())
assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
engine=pefile.PE(str(R/'ref/control/Tribes2.exe'));md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
records=[]
# Original fog-table instructions plus each index/scale, with and without a base.
for pc in [0x56a2db,0x56a2ee]:
 ins=next(md.disasm(engine.get_data(pc-0x400000,15),pc));assert ins.mnemonic in ('fst','fstp') and ins.operands[0].size==4
 records.append(dict(pc=pc,bytes_hex=bytes(ins.bytes).hex()))
for width in [32,64]:
 for pop in [0,1]:
  opcode=0xd9 if width==32 else 0xdd;reg=2+pop
  for index in [0,1,2,3,5,6,7]:
   for scale in range(4):
    for base in [3,5]:
     # Base=5/mod00 means no base; use an absolute unaligned displacement.
     raw=bytes([opcode,reg*8+4,scale*64+index*8+base])+(struct.pack('<I',0x10003) if base==5 else b'')
     records.append(dict(bytes_hex=raw.hex()))
  for base in range(8):
   raw=bytes([opcode,0x40+reg*8+base])+(b'\x24' if base==4 else b'')+b'\xef'
   records.append(dict(bytes_hex=raw.hex()))
run=fixture(out/'fixture',records,R/'ref/product/IFC22.dll',srw);assert run.returncode==0,(run.stdout,run.stderr)
s=(out/'fixture/seg01_code.llinc').read_text();procs=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',s,re.M|re.S));assert len(procs)==len(records)
source=out/'stores.llasm';source.write_text('include llasm_float.llinc\n'+''.join('proc store_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procs))
ir=out/'stores.ll';subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-o',str(ir),str(source)],check=True)
ir.write_text(ir.read_text().replace('define hidden fastcc void @store_','define ccc void @store_'))
sources=[R/n for n in ['port/core/ext80.c','port/llasm-support/tribes_float80.c','tests/x87_result_slot.c']]
libpath=out/'stores.dylib';sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
subprocess.run(['clang','-arch','arm64','-isysroot',sdk,'-O2','-dynamiclib','-Wno-override-module','-DPTROFS_64BIT','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(R/'ref/softfloat/SoftFloat-3e/source/include'),str(ir),*map(str,sources),str(R/'generated/softfloat/macos/softfloat.a'),str(R/'generated/bochs-math/libmath.dylib'),'-o',str(libpath)],check=True)
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('st',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx_active',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
assert ctypes.sizeof(CPU)==440
lib=ctypes.CDLL(str(libpath));lib.x87_fninit_void.argtypes=[ctypes.POINTER(CPU)];lib.x87_fld_double.argtypes=[ctypes.POINTER(CPU),ctypes.c_uint32,ctypes.c_uint32]
memory=ctypes.create_string_buffer(0xc00000);base=ctypes.addressof(memory);regnames=['eax','ecx','edx','ebx','esp','ebp','esi','edi'];cases=0
for n,record in enumerate(records):
 ins=next(md.disasm(bytes.fromhex(record['bytes_hex']),0));operand=ins.operands[0];assert operand.type==capstone.x86.X86_OP_MEM;mem=operand.mem;width=operand.size
 fn=getattr(lib,'store_'+str(n));fn.argtypes=[ctypes.POINTER(CPU)]
 for value in [0.0,0.25,0.875,1.0,-3.5,255.0,65536.0]:
  c=CPU();lib.x87_fninit_void(ctypes.byref(c));c.offset=base
  c.regs[:]=[0x10003,0x1013,0x101d,0x11005,0x13007,0x1019,0x101f,0x102f,0xad7]
  bits=struct.unpack('<Q',struct.pack('<d',value))[0];lib.x87_fld_double(ctypes.byref(c),bits&0xffffffff,bits>>32)
  address=mem.disp+(c.regs[regnames.index(ins.reg_name(mem.base))] if mem.base else 0)+(c.regs[regnames.index(ins.reg_name(mem.index))]*mem.scale if mem.index else 0)
  assert 16<=address<len(memory)-16-width and not address-16<=0x500000<address+width+16
  ctypes.memset(base+address-16,0xa5,width+32);before=bytes(c);fn(ctypes.byref(c));after=bytearray(bytes(c));expect=bytearray(before)
  assert ctypes.string_at(base+address,width)==struct.pack('<f' if width==4 else '<d',value),(record,ins.op_str,value,ctypes.string_at(base+address,width).hex())
  assert ctypes.string_at(base+address-16,16)==b'\xa5'*16 and ctypes.string_at(base+address+width,16)==b'\xa5'*16
  pop=ins.mnemonic=='fstp';assert c.top==(0 if pop else 7) and c.tag==(0xffff if pop else struct.unpack_from('<I',before,144)[0])
  assert c.sw==struct.unpack_from('<I',before,40)[0]
  for lo,hi in [(36,40),(144,148)]+([(120,128)] if width==8 else []):after[lo:hi]=expect[lo:hi]
  assert after==expect,('unrelated CPU change',record);cases+=1
report=dict(passed=True,blocks=len(records),cases=cases,original_sites=records[:2],source_sha256={str(p.relative_to(R)):sha(p) for p in sources+[pathlib.Path(__file__).resolve(),R/'scripts/patches/srw-tribes.patch']},srw_sha256=sha(srw),library_sha256=sha(libpath),scope='Fresh strict SRW/llasm execution of original fog FST/FSTP and 32/64-bit scaled, baseless, aliased and unaligned forms. Exact finite values, stack pop, unchanged unrelated CPU and destination canaries. Numerical edge cases belong to existing x87 store oracle suite; this is not full-engine acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(records),'indexed x87 store blocks,',cases,'native cases')
