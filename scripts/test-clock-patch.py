#!/usr/bin/env python3
"""Compare both clock callers, all clock blocks, and the complete timer path."""
import ctypes,json,mmap,pathlib,random,re,struct,subprocess
import capstone,unicorn
from capstone import x86_const as C
from unicorn import x86_const as X
from clock_patch_inputs import R,OUT,sha,load,definition,REGS
cpu,pages,read,engine,dll=load();meta=json.loads((OUT/'preparation.json').read_text())
for p,h in meta['inputs_sha256'].items():assert sha(R/p)==h,p
for p,h in meta['outputs_sha256'].items():assert sha(OUT/p)==h,p
(OUT/'test-result.json').write_text('{"passed":false,"state":"running"}\n')
main=sorted(meta['target_definitions'])+[r['name'] for r in meta['records']]
text=(OUT/'targets.ll').read_text()
text+='\n'+'\n'.join(definition((OUT/'engine.ll').read_text(),r['name']) for r in meta['records'])
isolated=text
for n in main:isolated,count=re.subn(r'(^define hidden fastcc void @)'+n+r'\(',lambda m:m[1]+'candidate_'+n+'(',isolated,flags=re.M);assert count==1
refs=set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(',isolated[isolated.index('define '):]))-{'candidate_'+n for n in main}
qpc=struct.unpack('<I',read(0x101290d4,4))[0]
targets={n:int(n[4:],16) for n in sorted(refs) if n.startswith('loc_')}
targets['tbimp_QueryPerformanceCounter']=qpc
assert refs-set(targets)=={'tribes_dispatch','tribes_set_dispatch_target'},refs
declared=set(re.findall(r'^declare.*@([A-Za-z_0-9]+)\(',isolated,re.M))
isolated+='\n'+''.join('declare hidden fastcc void @%s(%%_cpu*)\n'%n for n in sorted(set(targets)-declared))
(OUT/'isolated.ll').write_text(isolated)
mailbox='''define void @tribes_set_dispatch_target(ptr %cpu, i32 %target) { %p=getelementptr i8,ptr %cpu,i64 172
 store i32 %target,ptr %p
 ret void
}'''
bridge=['@observed_target=global i32 0','@observed_dispatch=global i32 0',mailbox]
for n,a in targets.items():bridge.append('define hidden fastcc void @%s(ptr %%cpu) { store i32 %d,ptr @observed_target\n ret void\n}'%(n,a))
bridge.append('''define hidden fastcc void @tribes_dispatch(ptr %cpu) { %p=getelementptr i8,ptr %cpu,i64 172
 %a=load i32,ptr %p
 store i32 %a,ptr @observed_target
 store i32 1,ptr @observed_dispatch
 ret void
}''')
for i,n in enumerate(main):bridge+=['declare hidden fastcc void @candidate_%s(ptr)'%n,'define void @enter_%d(ptr %%cpu) { call fastcc void @candidate_%s(ptr %%cpu)\n ret void\n}'%(i,n)]
(OUT/'isolated-bridge.ll').write_text('\n'.join(bridge)+'\n')
# Whole original path includes its signed 64-bit CRT arithmetic and cookie check.
whole=text+'\n'+'\n'.join(definition((OUT/'helpers.ll').read_text(),n) for n in meta['helper_definitions'])
fullnames=main+list(meta['helper_definitions'])
extra=set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(',whole[whole.index('define '):]))-set(fullnames)
assert extra=={'tribes_dispatch','tribes_set_dispatch_target','tbimp_QueryPerformanceCounter','x86_div_64','tb_shift_reg','loc_100F90D3'},extra
whole+='\ndeclare hidden fastcc void @loc_100F90D3(%_cpu*)\n'
(OUT/'whole.ll').write_text(whole)
bridge=['@observed_target=global i32 0',mailbox,'declare i32 @test_qpc(ptr)']
bridge+=['define hidden fastcc void @tbimp_QueryPerformanceCounter(ptr %cpu) { %pc=call i32 @test_qpc(ptr %cpu)\n call void @tribes_set_dispatch_target(ptr %cpu,i32 %pc)\n musttail call fastcc void @tribes_dispatch(ptr %cpu)\n ret void\n}',
         'define hidden fastcc void @loc_100F90D3(ptr %cpu) { store i32 269455571,ptr @observed_target\n ret void\n}']
bridge+=['declare hidden fastcc void @%s(ptr)'%n for n in fullnames]
bridge+=['define hidden fastcc void @tribes_dispatch(ptr %cpu) { %p=getelementptr i8,ptr %cpu,i64 172\n %pc=load i32,ptr %p\n switch i32 %pc,label %done [']
bridge+=['i32 %d,label %%route%d'%(int(n[4:],16),i) for i,n in enumerate(fullnames)]+[']']
bridge+=['route%d:\n musttail call fastcc void @%s(ptr %%cpu)\n ret void'%(i,n) for i,n in enumerate(fullnames)]
bridge+=['done:\n store i32 %pc,ptr @observed_target\n ret void\n}',
         'define void @whole_clock(ptr %cpu) { call fastcc void @loc_1005F0D0(ptr %cpu)\n ret void\n}']
(OUT/'whole-bridge.ll').write_text('\n'.join(bridge)+'\n')
fixture=OUT/'qpc.c';fixture.write_text('''#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "llasm_cpu.h"
uint64_t test_counter;
uint32_t test_qpc(_cpu *cpu){
 uint32_t args[2];memcpy(args,(void *)(uintptr_t)(cpu->_pointer_offset+cpu->_esp),8);
 assert(args[0]==0x1005f0ee);memcpy((void *)(uintptr_t)(cpu->_pointer_offset+args[1]),&test_counter,8);
 cpu->_eax=1;cpu->_esp+=8;return args[0];
}
''')
support=[R/'port/llasm-support/tribes_shift.c',R/'port/core/shift.c',R/'ref/halopad/port/llasm-support/llasm_divide.c']
for name,sources in [('isolated',['isolated.ll','isolated-bridge.ll']),('whole',['whole.ll','whole-bridge.ll','qpc.c'])]:
    command=['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-DPTROFS_64BIT','-dynamiclib','-I',str(R/'port/llasm-support'),*[str(OUT/p) for p in sources]]
    if name=='whole':command+=list(map(str,support))
    subprocess.run(command+['-o',str(OUT/(name+'.dylib'))],check=True)
rng=random.Random(0x1005f0d0);memory=mmap.mmap(-1,0x10500000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory))
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
regions=[(a,read(a,4096)) for a in [0x58a000,0x50c000,0x1005f000,0x100fa000,0x100f9000,0x10129000,0x101d5000,0x1046d000,0x10471000]]+[(0x200000,bytes(65536)),(0x300000,bytes(65536))]
for a,data in regions:u.mem_map(a,len(data))
boundaries=set(targets.values())|{0x201000}
for a in {a&~4095 for a in boundaries}-{a+offset for a,data in regions for offset in range(0,len(data),4096)}:u.mem_map(a,4096);u.mem_write(a,b'\xcc'*4096)
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
effects={};undefined=[0];steps=[0];expected_target=[0];complete=[False];visited_full=set();counter=[0]
flag_defs=[(bit,getattr(C,'X86_EFLAGS_UNDEFINED_'+n,0),sum(getattr(C,'X86_EFLAGS_'+action+'_'+n,0) for action in ['MODIFY','RESET','SET'])) for bit,n in [(1,'CF'),(4,'PF'),(16,'AF'),(64,'ZF'),(128,'SF'),(2048,'OF')]]
def code(machine,address,size,_):
    if complete[0] and address==qpc:
        sp=machine.reg_read(X.UC_X86_REG_ESP);pc,output=struct.unpack('<2I',machine.mem_read(sp,8));assert pc==0x1005f0ee
        machine.mem_write(output,struct.pack('<Q',counter[0]));machine.reg_write(X.UC_X86_REG_EAX,1);machine.reg_write(X.UC_X86_REG_ESP,sp+8);machine.reg_write(X.UC_X86_REG_EIP,pc);return
    if steps[0] and (address in {0x201000,0x100f90d3} if complete[0] else address in boundaries):expected_target[0]=address;machine.emu_stop();return
    steps[0]+=1
    if complete[0]:visited_full.add(address)
    if address not in effects:effects[address]=next(decoder.disasm(bytes(machine.mem_read(address,size)),address)).eflags
    for bit,undef,defined in flag_defs:
        if effects[address]&undef:undefined[0]|=bit
        elif effects[address]&defined:undefined[0]&=~bit
u.hook_add(unicorn.UC_HOOK_CODE,code)
def write(a,data):u.mem_write(a,data);ctypes.memmove(base+a,data,len(data))
def put(a,v,width=4):write(a,(v&((1<<(width*8))-1)).to_bytes(width,'little'))
def setup(sample,entry,full):
    for a,data in regions:write(a,data)
    for r in meta['records']:write(int(r['name'][4:],16),bytes.fromhex(r['after_bytes']))
    write(0x200000,rng.getrandbits(65536*8).to_bytes(65536,'little'));write(0x300000,rng.getrandbits(65536*8).to_bytes(65536,'little'))
    values=[0x301000,rng.getrandbits(32),rng.getrandbits(32),rng.getrandbits(32),0x208000+4*(sample%4),0x209000,rng.getrandbits(32),rng.getrandbits(32),0x202|rng.choice([0,0x8d5,0x41,0x884,0x400])]
    if entry not in ['loc_58A960']:values[0]=rng.getrandbits(32)
    put(values[4],0x201000);put(values[5],0x87654321);put(values[5]+4,0x201000)
    edge=[0,1,0x7fffffff,0x80000000,0xffffffff,0x100000000,0x7fffffffffffffff,0x8000000000000000,0xffffffffffffffff]
    start=edge[sample%len(edge)] if sample<81 else rng.getrandbits(64)
    delta=edge[(sample//len(edge))%len(edge)] if sample<81 else rng.getrandbits(64)
    now=(start+delta)&0xffffffffffffffff
    frequency=[1,1000,1000000000,0xffffffff,0x100000000,0x100000001,0x7fffffffffffffff][sample%7]
    put(0x1046d660,start,8);put(0x1046d658,frequency,8);put(0x101d5d40,rng.getrandbits(32))
    if entry=='loc_1005F0EE':put(values[4],now,8)
    if not full and entry=='loc_1005F132':put(values[5]+4,0x201000)
    c=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440)
    struct.pack_into('<9I',c,0,*values);struct.pack_into('<Q',c,112,base)
    for reg,v in zip(REGS,values):u.reg_write(reg,v)
    steps[0]=0;undefined[0]=0;expected_target[0]=0;complete[0]=full;counter[0]=now
    return c,delta,frequency
def compare(c,before,expected_pc,observed,dispatch):
    assert observed==expected_pc,(hex(observed),hex(expected_pc))
    expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in REGS])
    if dispatch:struct.pack_into('<I',expected,172,expected_pc)
    actual=bytearray(c.raw)
    for b in [actual,expected]:struct.pack_into('<I',b,32,struct.unpack_from('<I',b,32)[0]&~undefined[0])
    assert actual==expected,[(i,actual[i],expected[i]) for i in range(440) if actual[i]!=expected[i]]
    for a,data in regions:assert ctypes.string_at(base+a,len(data))==bytes(u.mem_read(a,len(data))),hex(a)
lib=ctypes.CDLL(str(OUT/'isolated.dylib'));observed=ctypes.c_uint32.in_dll(lib,'observed_target');dispatch=ctypes.c_uint32.in_dll(lib,'observed_dispatch')
per_entry={};visited=set();cases=0
for i,n in enumerate(main):
    fn=getattr(lib,'enter_%d'%i);fn.argtypes=[ctypes.c_void_p];seen=set()
    for sample in range(256):
        c,_,_=setup(sample,n,False);before=c.raw;u.emu_start(int(n[4:],16),0,count=128)
        assert expected_target[0] in boundaries,(n,sample)
        observed.value=dispatch.value=0;fn(c);compare(c,before,expected_target[0],observed.value,dispatch.value)
        seen.add(observed.value);visited.add(observed.value);cases+=1
    per_entry[n]=sorted(seen)
assert visited==boundaries,(visited,boundaries)
full=ctypes.CDLL(str(OUT/'whole.dylib'));fn=full.whole_clock;fn.argtypes=[ctypes.c_void_p]
observed=ctypes.c_uint32.in_dll(full,'observed_target');native_counter=ctypes.c_uint64.in_dll(full,'test_counter')
for sample in range(512):
    c,delta,frequency=setup(sample,'loc_1005F0D0',True);before=c.raw;native_counter.value=counter[0]
    u.emu_start(0x1005f0d0,0,count=2048);assert expected_target[0]==0x201000,(sample,hex(expected_target[0]))
    observed.value=0;fn(c);compare(c,before,0x201000,observed.value,True)
    product=(delta*1000)&0xffffffffffffffff
    if product>>63:product-=1<<64
    quotient=abs(product)//frequency*(-1 if product<0 else 1)
    assert struct.unpack_from('<I',c,0)[0]==quotient&0xffffffff,sample
assert {0x100fa630,0x100fa639,0x100fa331,0x100fa349,0x100fa38d}<=visited_full
paths=support+[R/'port/llasm-support/llasm_cpu.h',R/'scripts/clock_patch_inputs.py',R/'scripts/console_fade_inputs.py',pathlib.Path(__file__).resolve()]
report=dict(passed=True,cases=cases,definitions=len(main),per_entry=per_entry,visited_targets=sorted(visited),unvisited_targets=[],whole_path_cases=512,whole_path_blocks=len(fullnames),whole_path_visited_instructions=sorted(visited_full),
    preparation_sha256=sha(OUT/'preparation.json'),sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},output_sha256={n:sha(OUT/n) for n in ['isolated.ll','isolated-bridge.ll','isolated.dylib','whole.ll','whole-bridge.ll','whole.dylib','qpc.c']},
    scope='Both complete CALL blocks and five original clock blocks compared with original x86, all outgoing boundaries, full440CPU/defined flags and fixture memory. 512 complete clock executions include original signed64 CRT arithmetic and cookie validation, plus independent wrapped integer formula. QueryPerformanceCounter supplies identical deterministic input to both; live clock and native revision publication are separate checks.')
(OUT/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'isolated ARM64/x86 cases;',512,'complete clock paths with original CRT helpers and independent formula')
