#!/usr/bin/env python3
"""Test complete animation caller/target blocks, observing every outgoing edge."""
import ctypes,json,mmap,pathlib,random,re,struct,subprocess
import capstone,unicorn
from capstone import x86_const as C
from unicorn import x86_const as X
from console_animation_inputs import R,OUT,sha,load,definition,REGS
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
assert refs-set(targets)=={'tb_shift_reg','tb_sse_dq2pd','tb_sse_fence','tb_sse_pd2ps','tb_sse_scalarmem','tb_sse_setd','tb_sse_store','tribes_dispatch','tribes_set_dispatch_target','x86_mov_reg_mem_32'}
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
# Include both sides of signed/unsigned conversion boundaries and all four
# rounding modes. Source-bound x86 instructions arbitrate MXCSR status.
deltas=[0,1,125,250,500,1000,2000,10000,0x7fffff,0x800000,0xffffff,0x1000000,0x1000001,0x7fffffff,0x80000000,0x80000001,0xfffffffe,0xffffffff]
rng=random.Random(10002403);deltas += [rng.getrandbits(32) for _ in range(110)]
requests=[(d,0x1f80|(rounding<<13)|(0x8040 if i%2 else 0)|(i%64)) for rounding in range(4) for i,d in enumerate(deltas)]
oracle_source=R/'tests/console_animation_x86_oracle.c';oracle=OUT/'x86-oracle'
subprocess.run(['clang','-arch','x86_64','-O0',str(oracle_source),'-o',str(oracle)],check=True)
constants=OUT/'constants.bin';constants.write_bytes(read(0x101cb140,16)+read(0x101caf34,4))
request_path=OUT/'oracle-requests.txt';request_path.write_text(''.join('%08x %08x\n'%q for q in requests))
run=subprocess.run(['arch','-x86_64',str(oracle),str(constants)],input=request_path.read_text(),capture_output=True,text=True,check=True)
(OUT/'oracle.log').write_text(run.stdout+run.stderr);lines=run.stdout.splitlines();assert lines[0]=='translated=1' and len(lines)==len(requests)+1
oracle_rows=[tuple(int(v,16) for v in line.split()) for line in lines[1:]]
lib=ctypes.CDLL(str(libpath));observed=ctypes.c_uint32.in_dll(lib,'observed_target');dispatched=ctypes.c_uint32.in_dll(lib,'observed_dispatch')
memory=mmap.mmap(-1,0x10500000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));lib.tribes_absolute_memory_bind.argtypes=[ctypes.c_uint64];assert lib.tribes_absolute_memory_bind(base)==1
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
regions=[(p,read(p,4096)) for p in [0x67c000,0x67d000,0x10002000,0x101ca000,0x101cb000,0x10471000,0x9e8000]]+[(0x200000,bytes(65536)),(0x300000,bytes(65536))]
for a,data in regions:u.mem_map(a,len(data))
dynamic={0x426030,0x6bc5e0,0x6bc830,0x6bd050,0x6ba2a0,0x201000}
for p in {a&~4095 for a in dynamic}-{p for a,data in regions for p in range(a,a+len(data),4096)}:u.mem_map(p,4096)
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
visited=set();per_entry={};status_differences=[];cases=0
for index,name in enumerate(entries):
    fn=getattr(lib,'enter_%d'%index);fn.argtypes=[ctypes.c_void_p];seen=set()
    for sample in range(len(requests) if name=='loc_10002403' else 128):
        for a,data in regions:write(a,data)
        for row in meta['records']:write(int(row['name'][4:],16),bytes.fromhex(row['after_bytes']))
        write(0x200000,rng.getrandbits(65536*8).to_bytes(65536,'little'));write(0x300000,rng.getrandbits(65536*8).to_bytes(65536,'little'))
        values=[0x308000,0x300000,1000,0x300000,0x208000+4*(sample%2),0x209000,0x304000,[0,2,3,0xffffffff,0x7fffffff][sample%5],0x202|rng.choice([0,0x8d5,0x41,0x884,0x400])]
        for a,v in [(0x3001f8,0 if sample%2 else 0x304000),(0x3040c4,0 if sample%2 else 0x308000),(0x3080a4,3),(values[5]+8,values[7]),(0x304104,[0,1,0xffffffff][sample%3]),(0x30410c,0x30c000),(0x30c000,0 if sample%2 else 0x30d000),(0x9e8f7c,[999,1000,1001][sample%3]),(0x10471300,1000)]:i32(a,v)
        if name=='loc_100024C0':i32(values[4]+16,0x201000)
        if name=='loc_100024E5':i32(values[4]+12,0x201000)
        mxcsr=0x1f80|((sample%4)<<13)|(0x8040 if sample%2 else 0)|(sample%64)
        if name=='loc_10002403':
            delta,mxcsr=requests[sample];values[1]=0x125790;values[2]=(values[1]+delta)&0xffffffff
        cpu=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440)
        struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);struct.pack_into('<I',cpu,304,mxcsr)
        for reg,value in zip(REGS,values):u.reg_write(reg,value)
        for j in range(8):u.reg_write(getattr(X,'UC_X86_REG_XMM%d'%j),int.from_bytes(cpu.raw[176+16*j:192+16*j],'little'))
        u.reg_write(X.UC_X86_REG_MXCSR,mxcsr);before=cpu.raw;steps[0]=0;expected_target[0]=0;undefined[0]=0
        u.emu_start(int(name[4:],16),0,count=128);assert expected_target[0] in boundaries,(name,sample,'missing outgoing boundary')
        expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in REGS])
        for j in range(8):expected[176+16*j:192+16*j]=u.reg_read(getattr(X,'UC_X86_REG_XMM%d'%j)).to_bytes(16,'little')
        struct.pack_into('<I',expected,304,u.reg_read(X.UC_X86_REG_MXCSR))
        if name=='loc_10002403':
            reference=oracle_rows[sample];assert bytes(expected[176:192])==struct.pack('<4I',*reference[:4]),(sample,'x86 float result')
            assert bytes(u.mem_read(values[4]-4,4))==struct.pack('<I',reference[0])
            if u.reg_read(X.UC_X86_REG_MXCSR)!=reference[4]:status_differences.append(dict(sample=sample,unicorn=u.reg_read(X.UC_X86_REG_MXCSR),rosetta=reference[4]))
            struct.pack_into('<I',expected,304,reference[4])
        observed.value=0;dispatched.value=0;fn(cpu);assert observed.value==expected_target[0],(name,sample,'target',hex(observed.value),hex(expected_target[0]))
        if dispatched.value:struct.pack_into('<I',expected,172,observed.value)
        actual=bytearray(cpu.raw)
        for data in [actual,expected]:struct.pack_into('<I',data,32,struct.unpack_from('<I',data,32)[0]&~undefined[0])
        assert actual==expected,(name,sample,'CPU',[(j,actual[j],expected[j]) for j in range(440) if actual[j]!=expected[j]])
        for a,data in regions:assert ctypes.string_at(base+a,len(data))==bytes(u.mem_read(a,len(data))),(name,sample,'memory',hex(a))
        visited.add(observed.value);seen.add(observed.value);cases+=1
    per_entry[name]=sorted(seen)
assert visited==boundaries,('unvisited targets',sorted(boundaries-visited))
paths=support+[R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.h',R/'port/core/sse_arithmetic.c',R/'port/core/sse_arithmetic.h',R/'scripts/console_animation_inputs.py',R/'scripts/console_fade_inputs.py',oracle_source,pathlib.Path(__file__).resolve()]
report=dict(passed=True,cases=cases,definitions=len(entries),per_entry=per_entry,visited_targets=sorted(visited),unvisited_targets=sorted(boundaries-visited),
    preparation_sha256=sha(OUT/'preparation.json'),sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},math=math['evidence'],
    output_sha256={n:sha(OUT/n) for n in ['isolated.ll','test-bridge.ll','test.dylib','x86-oracle','constants.bin','oracle-requests.txt','oracle.log']},status_differences=status_differences,
    scope='Two complete redirected caller blocks and15 original target blocks compared in isolation with original x86. All outgoing targets exercised; full CPU (defined flags), XMM/MXCSR and fixture memory compared. Tick conversion uses an independent512-case Rosetta x86 SSE oracle including unsigned boundaries and all rounding modes. Callees observed, not executed; no complete path, renderer or runtime publication claim.')
(OUT/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'ARM64/x86 cases across',len(entries),'blocks,',len(visited),'outgoing targets;',len(status_differences),'recorded Unicorn/Rosetta MXCSR differences')
