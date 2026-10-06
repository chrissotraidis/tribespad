"""Exercise stale real translated code through a host-backed memory alias."""
import ctypes,hashlib,json,pathlib,struct,sys
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
from guest_cache_reference import CacheBoundary,ReferenceInstructionCache
from guest_page_contract import GuestPages,PageBoundary
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);storage=ctypes.create_string_buffer(8192);host=ctypes.addressof(storage);u.mem_map_ptr(0x1000,8192,unicorn.UC_PROT_ALL,host);pages=GuestPages(u);pages.declare(0x1000,8192,0x40,'owned_fixture');cache=ReferenceInstructionCache(u,pages)
def write(pc,value):ctypes.memmove(host+pc-0x1000,b'\xb8'+struct.pack('<I',value)+b'\xe9\xf6\xff\xff\xff',10)
def code_hook(emulator,pc,size,context):
 if pc in (0x1005,0x2005):emulator.emu_stop()
u.hook_add(unicorn.UC_HOOK_CODE,code_hook)
def execute(pc):
 cache.running=True
 try:u.emu_start(pc,0x3000,count=30)
 finally:cache.running=False
 return u.reg_read(X.UC_X86_REG_EAX)
write(0x1000,1);write(0x2000,10);assert execute(0x1000)==1 and execute(0x2000)==10
# Bypass Unicorn mem_write, which may automatically invalidate its code cache.
write(0x1000,2);write(0x2000,20);assert execute(0x1000)==1 and execute(0x2000)==10
before=bytes(storage);cache.invalidate(0x1000,6);assert execute(0x1000)==2 and execute(0x2000)==10 and bytes(storage)==before
cache.invalidate(0,0xffffffff);assert execute(0x2000)==20 and bytes(storage)==before
assert execute(0x1000)==2
write(0x1000,3);assert execute(0x1000)==2;cache.invalidate(0x1001,4);assert execute(0x1000)==3
cache.running=True
try:cache.invalidate(0x1000,6)
except CacheBoundary:pass
else:raise AssertionError("in-flight invalidation accepted")
cache.running=False
count=len(cache.events)
for address,size in [(0x1000,0),(0xffffffff,2),(0x100000000,1),(0x1000,-1),(0x3000,1)]:
 try:cache.invalidate(address,size)
 except (CacheBoundary,PageBoundary):pass
 else:raise AssertionError('unsupported range accepted')
assert len(cache.events)==count
report={'state':'finished','pass':True,'unicorn_version':unicorn.__version__,'sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [R/'scripts/guest_cache_reference.py',R/'scripts/guest_page_contract.py',pathlib.Path(__file__).resolve()]},'events':cache.events,'scope':'Actual stale x86 translation-cache behavior via host-backed memory alias; targeted/interior/global eviction refreshes executed instructions without writing bytes. No native ARM64 publication or Windows DLL parity claim.'};out=R/'generated/qol-initialization/cache-tests.json';out.write_text(json.dumps(report,indent=2)+'\n');print('PASS: stale x86 code retains old result until real bounded/global cache eviction; unrelated cached code preserved by bounded eviction; bytes unchanged; invalid/unmapped ranges reject without successful event')
