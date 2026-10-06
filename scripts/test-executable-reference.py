#!/usr/bin/env python3
"""Exercise real Unicorn capture mappings against the actual native owner query."""
import ctypes,hashlib,json,mmap,pathlib,subprocess,sys
import unicorn
from unicorn import x86_const as X
from guest_page_contract import GuestPages,PageBoundary
from guest_executable_reference import ExecutableReference
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/executable-reference-test';out.mkdir(exist_ok=True)
sources=[R/'port/core'/(n+'.c') for n in ['guest_address_space','guest_virtual_memory','process_heap','guest_heap']]
lib=out/'owners.dylib';subprocess.run(['clang','-dynamiclib','-O2','-Wall','-Wextra','-Werror',*map(str,sources),'-o',str(lib)],check=True,capture_output=True)
native=ctypes.CDLL(str(lib));window=mmap.mmap(-1,0x100000000,prot=mmap.PROT_READ|mmap.PROT_WRITE);base=ctypes.addressof(ctypes.c_char.from_buffer(window))
for name,count in [('tribes_address_space_begin',0),('tribes_address_space_declare',7),('tribes_address_space_seal',0),('tribes_virtual_query',3)]:
 fn=getattr(native,name);fn.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*count;fn.restype=ctypes.c_uint32
assert native.tribes_address_space_begin(base)
assert native.tribes_address_space_declare(base,0x100000,0x10000,0x100000,4,0x1000,4,0x20000)
assert native.tribes_address_space_declare(base,0x400000,0x2000,0x400000,0x80,0x1000,0x20,0x1000000)
assert native.tribes_address_space_seal(base)
def owner_query(address):
 assert native.tribes_virtual_query(base,address,0x100000,28)==28
 return list((ctypes.c_uint32*7).from_buffer(window,0x100000))
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);p=GuestPages(u)
for address,size,protect in [(0x100000,0x10000,4),(0x400000,0x2000,0x20),(0xfffc0000,4096,0x20)]:u.mem_map(address,size);p.declare(address,size,protect,address)
r=ExecutableReference(u,p,owner_query);assert r.allocate(0x3b0000,65536,0x3000,0x40)==0x3b0000
assert r.query(0x3b0123)==([0x3b0000,0x3b0000,0x40,65536,0x1000,0x40,0x20000],True)
assert r.query(0x3a0000)[0][3]==65536 and r.query(0x3a0000)[1]
rejections=0
for arguments in [(0x3b0000,65536,0x3000,0x40),(0x400000,65536,0x3000,0x40),(0x3c0001,65536,0x3000,0x40),(0x3c0000,4096,0x3000,0x40),(0x3c0000,65536,0x3000,0x20)]:
 try:r.allocate(*arguments)
 except PageBoundary:rejections+=1
 else:raise AssertionError(arguments)
assert rejections==5
r.protect(0x3b1000,4096,2,0x100101);assert bytes(u.mem_read(0x100101,4))==b'\x40\0\0\0';assert r.query(0x3b1001)[0]==[0x3b1000,0x3b0000,0x40,4096,0x1000,2,0x20000]
u.mem_write(0x3b0040,bytes.fromhex('b82a000000c3'));u.reg_write(X.UC_X86_REG_ESP,0x10fff0);u.mem_write(0x10fff0,(0xfffc0000).to_bytes(4,'little'))
seen=[]
def instruction(uc,address,size,_):
 seen.append(address)
 if address==0xfffc0000:uc.emu_stop()
u.hook_add(unicorn.UC_HOOK_CODE,instruction);u.emu_start(0x3b0040,0,count=20);assert u.reg_read(X.UC_X86_REG_EAX)==42 and u.reg_read(X.UC_X86_REG_ESP)==0x10fff4
u.mem_write(0x3b0080,bytes.fromhex('c70500103b0078563412'));u.reg_write(X.UC_X86_REG_ESP,0x10fff0)
try:u.emu_start(0x3b0080,0,count=1)
except unicorn.UcError as e:assert e.errno==unicorn.UC_ERR_WRITE_PROT
else:raise AssertionError('readonly guest page was writable')
assert r.release(0x3b0000,0,0x8000)==1 and r.query(0x3b0000)[0][4]==0x10000
try:u.mem_read(0x3b0040,1)
except unicorn.UcError:pass
else:raise AssertionError('released guest pages remained mapped')
code="import ctypes; n=ctypes.CDLL(%r); n.tribes_virtual_alloc.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*4; n.tribes_virtual_alloc(0,0x3b0000,65536,0x3000,0x40)"%str(lib)
fault=subprocess.run([sys.executable,'-c',code],capture_output=True,text=True);assert fault.returncode==-6 and 'classic executable guest allocation / patch routing' in fault.stderr
report={'pass':True,'state':'finished','rejected_allocations':rejections,'actual_x86_result':42,'executed_pcs':seen,'native_executable_allocation':{'returncode':fault.returncode,'state':'still explicit unsupported abort'},'sources_sha256':{str(s.relative_to(R)):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources+[R/'scripts/guest_executable_reference.py',R/'scripts/guest_page_contract.py',pathlib.Path(__file__).resolve()]},'scope':'Actual host Unicorn mapping/query/protection/execution/release and real native query ownership, no native ARM64 generated-code execution'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));window.close()
