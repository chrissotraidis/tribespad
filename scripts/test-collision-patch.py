#!/usr/bin/env python3
"""Test the complete collision-query caller and all original target blocks."""
import ctypes,json,mmap,pathlib,random,re,struct,subprocess,faulthandler
faulthandler.enable()
import capstone,unicorn
from capstone import x86_const as C
from unicorn import x86_const as X
from collision_patch_inputs import R,OUT,sha,load,definition,REGS
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
targets={n:int(n[4:],16) for n in sorted(refs) if n.startswith('loc_')}
assert refs-set(targets)=={'tb_sse_load','tb_sse_store','tb_sse_addps','tb_sse_mulpsmem','tb_sse_shufps','tb_sse_regmove','tribes_dispatch','tribes_set_dispatch_target','x87_fld_float','x87_fmul_float','x87_fadd_float','x87_fstp_float'},refs-set(targets)
declared=set(re.findall(r'^declare.*@([A-Za-z_0-9]+)\(',source,re.M))
source+='\n'+''.join('declare hidden fastcc void @%s(%%_cpu*)\n'%n for n in sorted(set(targets)-declared))
(OUT/'isolated.ll').write_text(source)
bridge=['@observed_target = global i32 0','@observed_dispatch = global i32 0']
for n,a in targets.items():bridge.append('define hidden fastcc void @%s(ptr %%cpu) { store i32 %d, ptr @observed_target\n ret void\n}'%(n,a))
bridge+=['define void @tribes_set_dispatch_target(ptr %cpu, i32 %target) { %p = getelementptr i8, ptr %cpu, i64 172\n store i32 %target, ptr %p\n ret void\n}', 'define hidden fastcc void @tribes_dispatch(ptr %cpu) { %p = getelementptr i8, ptr %cpu, i64 172\n %target = load i32, ptr %p\n store i32 %target, ptr @observed_target\n store i32 1, ptr @observed_dispatch\n ret void\n}']
for i,n in enumerate(entries):bridge+=['declare hidden fastcc void @candidate_%s(ptr)'%n,'define void @enter_%d(ptr %%cpu) { call fastcc void @candidate_%s(ptr %%cpu)\n ret void\n}'%(i,n)]
(OUT/'test-bridge.ll').write_text('\n'.join(bridge)+'\n')
math=sse_inputs('macos');support=[R/n for n in ['port/llasm-support/tribes_sse.c','port/llasm-support/tribes_absolute_memory.c','port/llasm-support/tribes_shift.c','port/core/shift.c','port/core/ext80.c','port/llasm-support/tribes_float80.c','port/llasm-support/tribes_result.c','generated/bochs-math/libmath.dylib']]
libpath=OUT/'test.dylib'
subprocess.run(['clang','-target','arm64-apple-macos26.0','-O2','-Wno-override-module','-DPTROFS_64BIT','-dynamiclib',str(OUT/'isolated.ll'),str(OUT/'test-bridge.ll'),*map(str,support),*math['compile_flags'],*math['sources'],*math['libraries'],'-o',str(libpath)],check=True)
rng=random.Random(0x10009900)
lib=ctypes.CDLL(str(libpath));observed=ctypes.c_uint32.in_dll(lib,'observed_target');dispatched=ctypes.c_uint32.in_dll(lib,'observed_dispatch')
memory=mmap.mmap(-1,0x10500000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));lib.tribes_absolute_memory_bind.argtypes=[ctypes.c_uint64];assert lib.tribes_absolute_memory_bind(base)==1
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
regions=[(p,read(p,4096)) for p in [0x617000,0x10009000,0x7a7000]]+[(0x200000,bytes(65536)),(0x300000,bytes(131072))]
for a,data in regions:u.mem_map(a,len(data))
dynamic={0x58d900,0x201000}
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
        values=[0x300000,0x300800,rng.getrandbits(32),0x301000,0x208000+4*(sample%4),0x209000,0x302000,4*(sample%4),0x202|rng.choice([0,0x8d5,0x41,0x884,0x400])]
        if name=='loc_10009937':values[4]&=~15
        for a,v in [(values[4],0x201000),(values[5]+4,0x201000),(values[5]+8,0x300800),(values[5]+12,0x301800),(values[5]+20,0x302000),(values[4]+20,0x302000),(0x302000,0x300000),(0x300028,[0,0x2000,0x1000000,0x1002000][sample%4]),(0x301098,0x304000),(values[5]-0x2c4,0x303000)]:i32(a,v)
        # Dyadic finite operands produce exact arithmetic, including zero,
        # negative values and cancellation, in all four rounding modes.
        def fp(a,value):write(a,struct.pack('<f',value))
        for j in range(16):fp(0x30009c+4*j,((sample*7+j*3)%33-16)/4)
        for j in range(3):
            fp(0x302010+4*j,((sample*3+j)%17-8)/2)
            fp(values[5]+values[7]-0x174+4*j,((sample+j)%33-16)/4)
            fp(values[5]+values[7]-0x184+4*j,((sample*7+j)%33-16)/4)
        fp(0x7a7660,0.5)
        if name=='loc_10009920':values[0]=(rng.getrandbits(24)<<8)|(sample%2)
        mxcsr=0x1f80|((sample%4)<<13)|(0x8040 if sample%2 else 0)|(sample%64)
        cpu=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440)
        struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);struct.pack_into('<I',cpu,304,mxcsr)
        for reg,value in zip(REGS,values):u.reg_write(reg,value)
        for j in range(8):u.reg_write(getattr(X,'UC_X86_REG_XMM%d'%j),int.from_bytes(cpu.raw[176+16*j:192+16*j],'little'))
        top=sample%8;cw=0x37f|(((sample//8)%4)<<10);tags=0xffff
        struct.pack_into('<3I',cpu,36,top,0,cw);struct.pack_into('<II',cpu,144,tags,0);struct.pack_into('<I',cpu,168,1)
        for j in range(8):
            significand=0x8000000000000000;exponent=0x3fff+j
            struct.pack_into('<Q',cpu,48+8*j,significand);struct.pack_into('<H',cpu,152+2*j,exponent);u.reg_write(getattr(X,'UC_X86_REG_FP%d'%j),(significand,exponent))
        u.reg_write(X.UC_X86_REG_FPCW,cw);u.reg_write(X.UC_X86_REG_FPSW,top<<11);u.reg_write(X.UC_X86_REG_FPTAG,tags)
        u.reg_write(X.UC_X86_REG_MXCSR,mxcsr);before=cpu.raw;steps[0]=0;expected_target[0]=0;undefined[0]=0
        u.emu_start(int(name[4:],16),0,count=128);assert expected_target[0] in boundaries,(name,sample,'missing outgoing boundary')
        expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in REGS])
        for j in range(8):expected[176+16*j:192+16*j]=u.reg_read(getattr(X,'UC_X86_REG_XMM%d'%j)).to_bytes(16,'little')
        struct.pack_into('<I',expected,304,u.reg_read(X.UC_X86_REG_MXCSR))
        sw=u.reg_read(X.UC_X86_REG_FPSW);struct.pack_into('<3I',expected,36,(sw>>11)&7,sw&~0x3800,u.reg_read(X.UC_X86_REG_FPCW));struct.pack_into('<I',expected,144,u.reg_read(X.UC_X86_REG_FPTAG))
        for j in range(8):
            mantissa,exponent=u.reg_read(getattr(X,'UC_X86_REG_FP%d'%j));struct.pack_into('<Q',expected,48+8*j,mantissa);struct.pack_into('<H',expected,152+2*j,exponent)
        observed.value=0;dispatched.value=0;fn(cpu);assert observed.value==expected_target[0],(name,sample,'target',hex(observed.value),hex(expected_target[0]))
        if dispatched.value:struct.pack_into('<I',expected,172,observed.value)
        actual=bytearray(cpu.raw)
        for data in [actual,expected]:struct.pack_into('<I',data,32,struct.unpack_from('<I',data,32)[0]&~undefined[0])
        assert actual==expected,(name,sample,'CPU',[(j,actual[j],expected[j]) for j in range(440) if actual[j]!=expected[j]])
        for a,data in regions:assert ctypes.string_at(base+a,len(data))==bytes(u.mem_read(a,len(data))),(name,sample,'memory',hex(a))
        visited.add(observed.value);seen.add(observed.value);cases+=1
    per_entry[name]=sorted(seen)
assert visited==boundaries,('unvisited targets',sorted(boundaries-visited))
paths=support+[R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.h',R/'port/core/sse_arithmetic.c',R/'port/core/sse_arithmetic.h',R/'scripts/collision_patch_inputs.py',R/'scripts/console_fade_inputs.py',pathlib.Path(__file__).resolve()]
report=dict(passed=True,cases=cases,definitions=len(entries),per_entry=per_entry,visited_targets=sorted(visited),unvisited_targets=sorted(boundaries-visited),
    preparation_sha256=sha(OUT/'preparation.json'),sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},math=math['evidence'],
    output_sha256={n:sha(OUT/n) for n in ['isolated.ll','test-bridge.ll','test.dylib']},
    scope='Complete redirected caller and six unchanged original target blocks compared in isolation with x86. Every outgoing target exercised, full440CPU including defined flags/x87/vector state and fixture memory checked. Exact dyadic arithmetic corpus covers four rounding modes. Callees observed; no whole-handler service semantics, runtime publication or authentication acceptance.')
(OUT/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'ARM64/x86 cases across',len(entries),'blocks,',len(visited),'outgoing targets')
