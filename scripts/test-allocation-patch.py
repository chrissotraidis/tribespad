#!/usr/bin/env python3
"""Compare the complete size-adjusted allocation block with original x86."""
import ctypes,json,mmap,pathlib,random,re,struct,subprocess
import unicorn
from unicorn import x86_const as X
from allocation_patch_inputs import R,OUT,sha,load,definition,REGS
cpu,pages,read,engine,dll=load();meta=json.loads((OUT/'preparation.json').read_text())
assert meta['passed'] and not meta['published']
for p,h in meta['inputs_sha256'].items():assert sha(R/p)==h,p
for p,h in meta['outputs_sha256'].items():assert sha(OUT/p)==h,p
(OUT/'test-result.json').write_text('{"passed":false,"state":"running"}\n')
source=(OUT/'engine.ll').read_text().replace('define hidden fastcc void @loc_615F00(','define hidden fastcc void @candidate_loc_615F00(',1)
body=definition(source,'candidate_loc_615F00')
assert set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(',body))=={'candidate_loc_615F00','loc_401FE0'}
(OUT/'isolated.ll').write_text(source)
bridge='''@observed_target = global i32 0
define hidden fastcc void @loc_401FE0(ptr %cpu) {
 store i32 4202464, ptr @observed_target
 ret void
}
declare hidden fastcc void @candidate_loc_615F00(ptr)
define void @enter_block(ptr %cpu) {
 call fastcc void @candidate_loc_615F00(ptr %cpu)
 ret void
}
'''
(OUT/'test-bridge.ll').write_text(bridge)
libpath=OUT/'test.dylib'
subprocess.run(['clang','-target','arm64-apple-macos26.0','-O2','-Wno-override-module','-dynamiclib',str(OUT/'isolated.ll'),str(OUT/'test-bridge.ll'),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));lib.enter_block.argtypes=[ctypes.c_void_p]
observed=ctypes.c_uint32.in_dll(lib,'observed_target')
memory=mmap.mmap(-1,0xb00000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory))
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
u.mem_map(0x615000,4096);u.mem_write(0x615000,read(0x615000,4096))
u.mem_write(0x615f00,bytes.fromhex(meta['records'][0]['after_bytes']))
u.mem_map(0x401000,4096);u.mem_write(0x401000,b"\xcc"*4096);u.mem_map(0x200000,65536)
steps=[];target=[None]
def code(machine,address,size,_):
    if address==0x401fe0:target[0]=address;machine.emu_stop();return
    assert 0x615f00<=address<0x615f1d,hex(address);steps.append(address)
u.hook_add(unicorn.UC_HOOK_CODE,code)
rng=random.Random(0x615f13);cases=0;visited=set()
for sample in range(256):
    data=rng.getrandbits(65536*8).to_bytes(65536,'little')
    u.mem_write(0x200000,data);ctypes.memmove(base+0x200000,data,len(data))
    state=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440)
    values=[rng.getrandbits(32) for _ in range(8)]+[0x202|(sample&1)|((sample&2)<<1)|((sample&4)<<2)|((sample&8)<<3)|((sample&16)<<3)|((sample&32)<<6)|((sample&64)<<4)]
    values[4]=0x204000+sample*4
    struct.pack_into('<9I',state,0,*values);struct.pack_into('<Q',state,112,base)
    for reg,value in zip(REGS,values):u.reg_write(reg,value)
    before=state.raw;steps.clear();target[0]=None
    try:u.emu_start(0x615f00,0,count=32)
    except unicorn.UcError as error:raise AssertionError((sample,steps,hex(u.reg_read(X.UC_X86_REG_EIP)),hex(u.reg_read(X.UC_X86_REG_ESP)),str(error)))
    assert target[0]==0x401fe0 and len(steps)==meta['instruction_count']
    expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in REGS])
    observed.value=0;lib.enter_block(state)
    assert observed.value==target[0]
    assert state.raw==expected,(sample,'CPU',[(i,a,b) for i,(a,b) in enumerate(zip(state.raw,expected)) if a!=b])
    assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536)),(sample,'stack')
    esp=struct.unpack_from('<I',state,16)[0]
    assert struct.unpack('<2I',ctypes.string_at(base+esp,8))==(0x615f1d,3884)
    visited.update(steps);cases+=1
report=dict(passed=True,cases=cases,definitions=1,visited_targets=[0x401fe0],unvisited_targets=[],instructions=sorted(visited),cpu_bytes=440,
    preparation_sha256=sha(OUT/'preparation.json'),sources_sha256={str(p.relative_to(R)):sha(p) for p in [R/'scripts/allocation_patch_inputs.py',R/'scripts/console_fade_inputs.py',pathlib.Path(__file__).resolve()]},
    output_sha256={n:sha(OUT/n) for n in ['isolated.ll','test-bridge.ll','test.dylib']},
    scope='Complete original allocation block with only the PUSH size changed, compared with x86 over256 inputs. All440 CPU bytes and64KiB stack checked; allocator entry, size and original return address observed. No allocator behavior, object lifetime or twelfth publication acceptance.')
(OUT/'test-result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS:',cases,'ARM64/x86 allocation-block cases;',len(visited),'instructions')
