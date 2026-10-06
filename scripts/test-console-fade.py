#!/usr/bin/env python3
"""Compare the first console CALL block and whole fade routine with x86."""
import ctypes, itertools, json, mmap, pathlib, random, re, struct, subprocess
import capstone, unicorn
from capstone import x86_const as C
from unicorn import x86_const as X
from console_fade_inputs import R, FROZEN, OUT, sha, load, definition
from sse_build_inputs import sse_inputs

read, pages, engine, dll = load()
meta = json.loads((OUT/'preparation.json').read_text())
assert meta['passed'] and not meta['published']
for path,h in meta['inputs_sha256'].items(): assert sha(R/path) == h
for path,h in meta['outputs_sha256'].items(): assert sha(OUT/path) == h
(OUT/'test-result.json').write_text('{"passed":false,"state":"running"}\n')
fade = (OUT/'fade.ll').read_text()
block = definition((OUT/'engine.ll').read_text(), 'loc_5D2842')
# Isolate the call block with an observer; the whole routine keeps its CFG.
block = block.replace('@loc_10002550(', '@observe_fade_target(')
source = fade + '\ndeclare hidden fastcc void @observe_fade_target(%_cpu*)\n' + block
(OUT/'test.ll').write_text(source)
bridge = '''@observed_target = global i32 0
define hidden fastcc void @observe_fade_target(ptr %cpu) {
 store i32 268445008, ptr @observed_target
 ret void
}
define void @tribes_set_dispatch_target(ptr %cpu, i32 %target) {
 %p = getelementptr i8, ptr %cpu, i64 172
 store i32 %target, ptr %p
 ret void
}
define hidden fastcc void @tribes_dispatch(ptr %cpu) {
 %p = getelementptr i8, ptr %cpu, i64 172
 %target = load i32, ptr %p
 store i32 %target, ptr @observed_target
 ret void
}
declare hidden fastcc void @loc_10002550(ptr)
declare hidden fastcc void @loc_5D2842(ptr)
define void @enter_fade(ptr %cpu) {
 call fastcc void @loc_10002550(ptr %cpu)
 ret void
}
define void @enter_block(ptr %cpu) {
 call fastcc void @loc_5D2842(ptr %cpu)
 ret void
}
'''
(OUT/'test-bridge.ll').write_text(bridge)
math = sse_inputs('macos')
support = [R/'port/llasm-support/tribes_sse.c', R/'port/llasm-support/tribes_absolute_memory.c']
libpath = OUT/'test.dylib'
command = ['clang','-target','arm64-apple-macos26.0','-O2','-Wno-override-module','-DPTROFS_64BIT','-dynamiclib',str(OUT/'test.ll'),str(OUT/'test-bridge.ll')] + list(map(str,support)) + math['compile_flags'] + math['sources'] + math['libraries'] + ['-o',str(libpath)]
subprocess.run(command, check=True)
lib = ctypes.CDLL(str(libpath)); observed = ctypes.c_uint32.in_dll(lib,'observed_target')
lib.enter_block.argtypes = lib.enter_fade.argtypes = [ctypes.c_void_p]
memory = mmap.mmap(-1, 0x10200000)
base = ctypes.addressof(ctypes.c_char.from_buffer(memory))
lib.tribes_absolute_memory_bind.argtypes = [ctypes.c_uint64]
assert lib.tribes_absolute_memory_bind(base) == 1
u = unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
regions = [(p, read(p,4096)) for p in [0x5d2000,0x10002000,0x101ca000,0x101cb000,0x101d6000,0x9e8000,0x79b000,0x7a1000]]
regions += [(0x200000,bytes(65536)),(0x300000,bytes(0x3000)),(0x400000,bytes(0x9000))]
for address,data in regions: u.mem_map(address,len(data))
RETURN = 0x201000
regs = [getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
decoder = capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); decoder.detail = True
effects = {}; undefined = [0]; stopped = [False]; stop = [0]; visited = set(); steps = [0]
flag_defs = [(bit,getattr(C,'X86_EFLAGS_UNDEFINED_'+name,0),sum(getattr(C,'X86_EFLAGS_'+action+'_'+name,0) for action in ['MODIFY','RESET','SET'])) for bit,name in [(1,'CF'),(4,'PF'),(16,'AF'),(64,'ZF'),(128,'SF'),(2048,'OF')]]
def code(machine,address,size,_):
    if address == stop[0]: stopped[0] = True; machine.emu_stop(); return
    steps[0] += 1; visited.add(address)
    if address not in effects:
        effects[address] = next(decoder.disasm(bytes(machine.mem_read(address,size)),address)).eflags
    for bit,undef,defined in flag_defs:
        if effects[address] & undef: undefined[0] |= bit
        elif effects[address] & defined: undefined[0] &= ~bit
u.hook_add(unicorn.UC_HOOK_CODE,code)
rng = random.Random(10002550)
def write(address,data):
    u.mem_write(address,data); ctypes.memmove(base+address,data,len(data))
def i32(address,value): write(address,struct.pack('<I',value))
def f32(address,value): write(address,struct.pack('<f',value))
def run(kind,index,fixture=None):
    for address,data in regions: write(address,data)
    write(0x5d2842,bytes.fromhex(meta['after_bytes']))
    write(0x200000,rng.getrandbits(65536*8).to_bytes(65536,'little'))
    write(0x300000,rng.getrandbits(0x3000*8).to_bytes(0x3000,'little'))
    values = [rng.getrandbits(32) for _ in range(8)] + [0x202 | rng.choice([0,0x8d5,0x41,0x884,0x400])]
    values[1] = 0x300000; values[3] = 0x301000; values[4] = 0x208000 + 4*(index%2); values[5] = 0x209000
    i32(values[4], RETURN)
    if fixture:
        root,enabled,third,override,active,direction,rendered,pref,current,elapsed,duration = fixture
        i32(0x9e8c7c,0 if root==0 else 0x400000)
        i32(0x40825c,0x300000 if root==1 else 0x301000)
        for address,value in [(0x7a1a40,enabled),(0x79b3e4,third),(0x79b3e8,override),(0x300819,active),(0x300818,direction),(0x3002c8,rendered)]: write(address,bytes([value]))
        for address,value in [(0x101d6864,pref),(0x30081c,current),(0x300820,elapsed),(0x300824,duration)]: f32(address,value)
    cpu = ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440)
    struct.pack_into('<9I',cpu,0,*values); struct.pack_into('<Q',cpu,112,base)
    mxcsr = 0x1f80 | ((index%4)<<13) | (0x8040 if index%2 else 0) | (index%64)
    struct.pack_into('<I',cpu,304,mxcsr); u.reg_write(X.UC_X86_REG_MXCSR,mxcsr)
    for reg,value in zip(regs,values): u.reg_write(reg,value)
    for j in range(8): u.reg_write(getattr(X,'UC_X86_REG_XMM%d'%j),int.from_bytes(cpu.raw[176+j*16:192+j*16],'little'))
    before = cpu.raw; stop[0] = RETURN if kind=='fade' else 0x10002550
    stopped[0] = False; steps[0] = 0; undefined[0] = 0; observed.value = 0
    u.emu_start(0x10002550 if kind=='fade' else 0x5d2842,0,count=256)
    assert stopped[0], (kind,index,'missing return/transfer')
    expected = bytearray(before)
    struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs])
    for j in range(8): expected[176+j*16:192+j*16] = u.reg_read(getattr(X,'UC_X86_REG_XMM%d'%j)).to_bytes(16,'little')
    struct.pack_into('<I',expected,304,u.reg_read(X.UC_X86_REG_MXCSR))
    if kind=='fade': struct.pack_into('<I',expected,172,RETURN)
    (lib.enter_fade if kind=='fade' else lib.enter_block)(cpu)
    assert observed.value==stop[0], (kind,index,'target',hex(observed.value),hex(stop[0]))
    actual = bytearray(cpu.raw)
    for data in [actual,expected]: struct.pack_into('<I',data,32,struct.unpack_from('<I',data,32)[0]&~undefined[0])
    assert actual == expected, (kind,index,fixture,'CPU',[(j,actual[j],expected[j]) for j in range(440) if actual[j]!=expected[j]])
    for address,data in regions:
        assert ctypes.string_at(base+address,len(data)) == bytes(u.mem_read(address,len(data))), (kind,index,fixture,'memory',hex(address))
    return steps[0]
instructions = sum(run('block',i) for i in range(256))
# Dyadic values make every tested arithmetic result exactly representable.
# The whole-function oracle includes MXCSR; it does not waive exception bits.
fixtures = []
for root,enabled,third,override,active,direction,rendered in itertools.product(range(3),range(2),range(2),range(2),range(2),range(2),range(2)):
    for pref,current in [(0.5,1),(0.5,0),(0.5,1+2**-20),(0.5,2),(0,0),(1,1),(0.5,2**-20)]:
        fixtures.append((root,enabled,third,override,active,direction,rendered,pref,current,0.5,2))
for _ in range(2048):
    fixtures.append((1,*[rng.randrange(2) for _ in range(6)],rng.choice([-0.5,0,0.25,0.5,0.75,1,2]),rng.choice([0,2**-20,0.25,0.5,1-2**-20,1,1+2**-20,1.25,2]),rng.choice([-1,0,0.25,0.5,1,2]),rng.choice([0.5,1,2,4])))
for i,fixture in enumerate(fixtures): instructions += run('fade',i,fixture)
entries = {int(n[4:],16) for n in meta['fade_definitions']}
assert entries <= visited, ('unvisited definitions', sorted(entries-visited))
original_instructions = {i.address for i in decoder.disasm(read(0x10002550,0x13c),0x10002550) if i.mnemonic!='int3'}
assert original_instructions <= visited, ('unvisited original instructions', sorted(original_instructions-visited))
paths = support + [R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.h',R/'port/core/sse_arithmetic.c',R/'port/core/sse_arithmetic.h',R/'scripts/console_fade_inputs.py',pathlib.Path(__file__).resolve()]
report = dict(passed=True,call_block_cases=256,whole_fade_cases=len(fixtures),instructions=instructions,visited_fade_entries=sorted(entries),visited_fade_instructions=sorted(original_instructions),
    preparation_sha256=sha(OUT/'preparation.json'),sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},math=math['evidence'],
    binary_sha256=sha(libpath),test_module_sha256=sha(OUT/'test.ll'),bridge_sha256=sha(OUT/'test-bridge.ll'),
    scope='Isolated complete CALL block plus whole original 20-block routine on ARM64 versus original x86 in Unicorn. All 440 CPU bytes (defined EFLAGS), XMMs, MXCSR and all fixture memory compared; every routine instruction visited. Finite exact dyadic corpus, all four rounding modes, DAZ/FTZ and preexisting status bits. Does not claim all numeric values, unmasked exceptions, native publication, whole console initializer or rendering acceptance.')
(OUT/'test-result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 256 CALL cases and',len(fixtures),'whole fade cases; all 20 blocks and',len(original_instructions),'instructions covered')
