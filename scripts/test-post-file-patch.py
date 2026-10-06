#!/usr/bin/env python3
"""Test the complete early-command caller and all original target blocks."""
import ctypes,json,mmap,pathlib,random,re,struct,subprocess
import capstone,unicorn
from capstone import x86_const as C
from unicorn import x86_const as X
from post_file_patch_inputs import R,OUT,sha,load,definition,REGS
from sse_build_inputs import sse_inputs
cpu,pages,read,engine,dll=load();meta=json.loads((OUT/'preparation.json').read_text())
assert meta['passed'] and not meta['published']
for p,h in meta['inputs_sha256'].items():assert sha(R/p)==h
for p,h in meta['outputs_sha256'].items():assert sha(OUT/p)==h
(OUT/'test-result.json').write_text('{"passed":false,"state":"running"}\n')
source=(OUT/'targets.ll').read_text();entries=sorted(meta['target_definitions'])
for r in meta['records']:source+='\n'+definition((OUT/'engine.ll').read_text(),r['name']);entries.append(r['name'])
for name in entries:
    source,n=re.subn(r'(^define (?:hidden|protected) fastcc void @)'+name+r'\(',lambda m:m[1]+'candidate_'+name+'(',source,flags=re.M);assert n==1
bodies=source[source.index('define '):]
refs=set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(',bodies))-{'candidate_'+n for n in entries}
targets={n:int(n[4:],16) for n in refs if n.startswith('loc_')}
assert refs-set(targets)=={'tb_sse_fence','tribes_dispatch','tribes_set_dispatch_target'}
declared=set(re.findall(r'^declare.*@([A-Za-z_0-9]+)\(',source,re.M))
source+='\n'+''.join('declare hidden fastcc void @%s(%%_cpu*)\n'%n for n in sorted(set(targets)-declared))
(OUT/'isolated.ll').write_text(source)
bridge=['@observed_target = global i32 0','@observed_dispatch = global i32 0']
for n,a in targets.items():bridge.append('define hidden fastcc void @%s(ptr %%cpu) { store i32 %d, ptr @observed_target\n ret void\n}'%(n,a))
bridge+=['define void @tribes_set_dispatch_target(ptr %cpu, i32 %target) { %p = getelementptr i8, ptr %cpu, i64 172\n store i32 %target, ptr %p\n ret void\n}', 'define hidden fastcc void @tribes_dispatch(ptr %cpu) { %p = getelementptr i8, ptr %cpu, i64 172\n %target = load i32, ptr %p\n store i32 %target, ptr @observed_target\n store i32 1, ptr @observed_dispatch\n ret void\n}']
for i,n in enumerate(entries):bridge+=['declare hidden fastcc void @candidate_%s(ptr)'%n,'define void @enter_%d(ptr %%cpu) { call fastcc void @candidate_%s(ptr %%cpu)\n ret void\n}'%(i,n)]
(OUT/'test-bridge.ll').write_text('\n'.join(bridge)+'\n')
math=sse_inputs('macos');support=[R/n for n in ['port/llasm-support/tribes_sse.c','port/llasm-support/tribes_absolute_memory.c','port/llasm-support/tribes_shift.c','port/core/shift.c']]
libpath=OUT/'test.dylib'
subprocess.run(['clang','-target','arm64-apple-macos26.0','-O2','-Wno-override-module','-DPTROFS_64BIT','-dynamiclib',str(OUT/'isolated.ll'),str(OUT/'test-bridge.ll'),*map(str,support),*math['compile_flags'],*math['sources'],*math['libraries'],'-o',str(libpath)],check=True)
rng=random.Random(0x10015a10)
lib=ctypes.CDLL(str(libpath));observed=ctypes.c_uint32.in_dll(lib,'observed_target');dispatched=ctypes.c_uint32.in_dll(lib,'observed_dispatch')
memory=mmap.mmap(-1,0x10500000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));lib.tribes_absolute_memory_bind.argtypes=[ctypes.c_uint64];assert lib.tribes_absolute_memory_bind(base)==1
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
regions=[(p,read(p,4096)) for p in [0x5bf000,0x10015000,0x101ba000,0x101b6000]]+[(0x200000,bytes(65536)),(0x300000,bytes(131072))]
for a,data in regions:u.mem_map(a,len(data))
dynamic={0x439550,0x42cb20,0x4268d0,0x4267a0,0x201000}
for p in {a&~4095 for a in dynamic|set(targets.values())}-{p for a,data in regions for p in range(a,a+len(data),4096)}:u.mem_map(p,4096)
boundaries=set(targets.values())|dynamic
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True;effects={};undefined=[0];steps=[0];expected_target=[0]
flag_defs=[(bit,getattr(C,'X86_EFLAGS_UNDEFINED_'+n,0),sum(getattr(C,'X86_EFLAGS_'+action+'_'+n,0) for action in ['MODIFY','RESET','SET'])) for bit,n in [(1,'CF'),(4,'PF'),(16,'AF'),(64,'ZF'),(128,'SF'),(2048,'OF')]]
def code(machine,address,size,_):
    if steps[0] and address in boundaries:expected_target[0]=address;machine.emu_stop();return
    steps[0]+=1
    if address not in effects:
        ins=next(decoder.disasm(bytes(machine.mem_read(address,size)),address))
        # Capstone's static flag mask does not distinguish SHR's immediate
        # count. OF is architecturally undefined when this count exceeds1.
        count_undefined=0x800 if ins.mnemonic=='shr' and ins.operands[-1].type==C.X86_OP_IMM and (ins.operands[-1].imm&31)>1 else 0
        effects[address]=(ins.eflags,count_undefined)
    for bit,undef,defined in flag_defs:
        if effects[address][0]&undef:undefined[0]|=bit
        elif effects[address][0]&defined:undefined[0]&=~bit
    undefined[0]|=effects[address][1]
u.hook_add(unicorn.UC_HOOK_CODE,code)
def write(a,data):u.mem_write(a,data);ctypes.memmove(base+a,data,len(data))
def i32(a,v):write(a,struct.pack('<I',v&0xffffffff))
visited=set();per_entry={};cases=0
for index,name in enumerate(entries):
    fn=getattr(lib,'enter_%d'%index);fn.argtypes=[ctypes.c_void_p];seen=set()
    for sample in range(128):
        for a,data in regions:write(a,data)
        for row in meta['records']:write(int(row['name'][4:],16),bytes.fromhex(row['after_bytes']))
        write(0x200000,rng.getrandbits(65536*8).to_bytes(65536,'little'));write(0x300000,rng.getrandbits(131072*8).to_bytes(131072,'little'))
        values=[0 if sample%2 else 0x306000,0x300801,rng.getrandbits(32),0x300000,0x208000+4*(sample%2),0x209000,0x300800,0x300800,0x202|rng.choice([0,0x8d5,0x41,0x884,0x400])]
        for a,v in [(values[4]+8,0x300000),(0x300000,0x300800),(0x300004,0x300a00),(values[5]+4,0x201000),(values[5]-4,0x300800),(0x300190,sample%2),(0x308428,0 if sample%2 else 1),(0x308368,0xffffffff if sample%2 else 0),(0x3000c0,rng.getrandbits(32))]:i32(a,v)
        write(0x300800,(b'serverCmd'+b't2csri_' if sample%2 else b'otherCmd')+b'Example\0')
        write(0x300a00,b'12345\0')
        if name=='loc_10015A24':write(0x300800,bytes([sample&255,0]))
        if name=='loc_10015A2B':values[6]=values[1]+[0,1,8,9,10,16,17,0xffffffff][sample%8];values[6]&=0xffffffff
        if name=='loc_10015A67':values[0]=[0,0x3000a0,0xa0,0x308000][sample%4]
        if name=='loc_10015A99':values[6]=[0,9,10,15,16,17,32,0xffffffff][sample%8]
        mxcsr=0x1f80|((sample%4)<<13)|(0x8040 if sample%2 else 0)|(sample%64)
        cpu=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440)
        struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);struct.pack_into('<I',cpu,304,mxcsr)
        for reg,value in zip(REGS,values):u.reg_write(reg,value)
        for j in range(8):u.reg_write(getattr(X,'UC_X86_REG_XMM%d'%j),int.from_bytes(cpu.raw[176+16*j:192+16*j],'little'))
        u.reg_write(X.UC_X86_REG_MXCSR,mxcsr);before=cpu.raw;steps[0]=0;expected_target[0]=0;undefined[0]=0
        u.emu_start(int(name[4:],16),0,count=128);assert expected_target[0] in boundaries,(name,sample,'missing outgoing boundary')
        expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in REGS])
        for j in range(8):expected[176+16*j:192+16*j]=u.reg_read(getattr(X,'UC_X86_REG_XMM%d'%j)).to_bytes(16,'little')
        struct.pack_into('<I',expected,304,u.reg_read(X.UC_X86_REG_MXCSR))
        observed.value=0;dispatched.value=0;fn(cpu);assert observed.value==expected_target[0],(name,sample,'target',hex(observed.value),hex(expected_target[0]))
        if dispatched.value:struct.pack_into('<I',expected,172,observed.value)
        actual=bytearray(cpu.raw)
        for data in [actual,expected]:struct.pack_into('<I',data,32,struct.unpack_from('<I',data,32)[0]&~undefined[0])
        assert actual==expected,(name,sample,'CPU',[(j,actual[j],expected[j]) for j in range(440) if actual[j]!=expected[j]])
        for a,data in regions:assert ctypes.string_at(base+a,len(data))==bytes(u.mem_read(a,len(data))),(name,sample,'memory',hex(a))
        visited.add(observed.value);seen.add(observed.value);cases+=1
    per_entry[name]=sorted(seen)
assert visited==boundaries,('unvisited targets',sorted(boundaries-visited))
paths=support+[R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.h',R/'port/core/sse_arithmetic.c',R/'port/core/sse_arithmetic.h',R/'scripts/post_file_patch_inputs.py',R/'scripts/console_fade_inputs.py',pathlib.Path(__file__).resolve()]
report=dict(passed=True,cases=cases,definitions=len(entries),per_entry=per_entry,visited_targets=sorted(visited),unvisited_targets=sorted(boundaries-visited),
    preparation_sha256=sha(OUT/'preparation.json'),sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},math=math['evidence'],
    output_sha256={n:sha(OUT/n) for n in ['isolated.ll','test-bridge.ll','test.dylib']},
    scope='Complete redirected caller and21 unchanged original target blocks compared in isolation with x86. Every outgoing target exercised, full440CPU including defined flags/vector state and fixture memory checked. Callees observed; no whole-handler service semantics, runtime publication or authentication acceptance.')
(OUT/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'ARM64/x86 cases across',len(entries),'blocks,',len(visited),'outgoing targets')
