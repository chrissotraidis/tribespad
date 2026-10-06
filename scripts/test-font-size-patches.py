#!/usr/bin/env python3
"""Compare both independently selected font allocation blocks to original x86."""
import ctypes,json,mmap,pathlib,random,re,struct,subprocess
import unicorn
from unicorn import x86_const as X
from font_size_runtime_inputs import R,ROWS,prepare,sha
from console_fade_inputs import definition
OUT=R/'generated/integration-review/font-size';OUT.mkdir(exist_ok=True)
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';dll=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll'
original=engine.read_text();staging=OUT/'engine-routed.ll';staging.write_text(original)
_,variant,meta=prepare(dll.read_text(),staging,OUT,verify_test=False)
routed=staging.read_text();header=original[:original.index('define ')]
blocks=[]
for _,_,name,_ in ROWS:
 blocks += [definition(routed,'tribes_before_font_'+name),definition(routed,name)]
blocks += [definition(variant.read_text(),'tribes_font_'+name) for _,_,name,_ in ROWS]
bridge='''@route_mask = global i32 0
@observed_target = global i32 0
define i32 @qol_font_size_route_ready(i32 %index) {
 %mask = load i32, ptr @route_mask
 %shift = lshr i32 %mask, %index
 %enabled = and i32 %shift, 1
 ret i32 %enabled
}
define hidden fastcc void @loc_401FE0(%_cpu* %cpu) { store i32 4202464, ptr @observed_target
 ret void
}
'''
for index,(_,_,name,_) in enumerate(ROWS):bridge+='define void @enter_%d(%%_cpu* %%cpu) { call fastcc void @%s(%%_cpu* %%cpu)\n ret void\n}\n'%(index,name)
source=OUT/'isolated.ll';source.write_text(header+'\n'.join(blocks)+'\n'+bridge)
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
libpath=OUT/'test.dylib';subprocess.run(['clang','-isysroot',sdk,'-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(source),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));mask=ctypes.c_uint32.in_dll(lib,'route_mask');observed=ctypes.c_uint32.in_dll(lib,'observed_target')
mem=mmap.mmap(-1,0xb00000);base=ctypes.addressof(ctypes.c_char.from_buffer(mem));rng=random.Random(0x556029)
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];cases=0
for index,row in enumerate(meta['records']):
 entry=int(row['name'][4:],16);raw=bytes.fromhex(row['original_bytes']);call=row['end']-5
 u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(entry&~4095,4096);u.mem_map(0x401000,4096);u.mem_write(0x401000,b"\xcc"*4096);u.mem_map(0x200000,65536)
 u.hook_add(unicorn.UC_HOOK_CODE,lambda machine,address,size,_:machine.emu_stop() if address==0x401fe0 else None)
 enter=getattr(lib,'enter_%d'%index);enter.argtypes=[ctypes.c_void_p]
 for enabled in range(4):
  code=bytearray(raw);size=568 if enabled&(1<<index) else 544;struct.pack_into('<I',code,row['address']-entry,size);u.mem_write(entry,bytes(code));u.ctl_remove_cache(entry,entry+len(code))
  for sample in range(256):
   data=rng.getrandbits(65536*8).to_bytes(65536,'little');u.mem_write(0x200000,data);ctypes.memmove(base+0x200000,data,65536)
   state=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);values=[rng.getrandbits(32) for _ in range(8)]+[0x202|(sample&1)|((sample&2)<<1)|((sample&4)<<2)|((sample&8)<<3)|((sample&16)<<3)|((sample&32)<<6)|((sample&64)<<4)];values[4]=0x204000+sample*4
   struct.pack_into('<9I',state,0,*values);struct.pack_into('<Q',state,112,base)
   for reg,value in zip(regs,values):u.reg_write(reg,value)
   expected=bytearray(state.raw);u.emu_start(entry,0,count=64);assert u.reg_read(X.UC_X86_REG_EIP)==0x401fe0
   struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);mask.value=enabled;observed.value=0;enter(state)
   assert observed.value==0x401fe0 and state.raw==expected,(index,enabled,sample,'CPU')
   assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536)),(index,enabled,sample,'stack')
   esp=struct.unpack_from('<I',state,16)[0];assert struct.unpack('<2I',ctypes.string_at(base+esp,8))==(row['end'],size);cases+=1
report=dict(passed=True,cases=cases,records=meta['records'],inputs_sha256=meta['inputs_sha256'],test_script_sha256=sha(pathlib.Path(__file__).resolve()),binary_sha256=sha(libpath),scope='Both complete original allocation blocks, all four independent activation combinations, compared with x86 across2048 cases: all440 CPU bytes,64KiB stack, allocator target, original return address and size. Native publication separately exercised by code-revision and full-engine tests.')
(OUT/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'font allocation ARM64/x86 comparisons')
