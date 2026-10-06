#!/usr/bin/env python3
"""Compare the strict replacement body with the original patched x86 block."""
import ctypes,hashlib,json,pathlib,random,struct,subprocess
import pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/startup-input-variant';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((out/'result.json').read_text());assert m['state']=='strict_unpublished_variant' and sha(out/'variant.ll')==m['llvm_sha256']
original=R/'ref/control/Tribes2.exe';assert sha(original)==m['original_sha256'];pe=pefile.PE(str(original));code=pe.get_data(0x156cff,7);assert code.hex()=='50'+m['patch_before'];code=code[:1]+bytes.fromhex(m['patch_after'])
bridge=out/'test-bridge.ll';bridge.write_text('''@target = global i32 0
declare hidden fastcc void @patched_input_block(ptr)
define hidden fastcc void @unchanged_next(ptr %cpu) { store i32 5598470, ptr @target
 ret void
}
define void @enter(ptr %cpu) { call fastcc void @patched_input_block(ptr %cpu)
 ret void
}
''')
libpath=out/'test.dylib';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(out/'variant.ll'),str(bridge),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));lib.enter.argtypes=[ctypes.c_void_p];target=ctypes.c_uint32.in_dll(lib,'target')
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x556000,4096);u.mem_write(0x556cff,code);u.mem_map(0x200000,65536)
memory=ctypes.create_string_buffer(0x210000);base=ctypes.addressof(memory);rng=random.Random(25034);regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
for sample in range(512):
 values=[rng.getrandbits(32) for _ in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884,0x400])];values[4]=0x208000+(sample%64)
 stack=rng.getrandbits(65536*8).to_bytes(65536,'little');u.mem_write(0x200000,stack);ctypes.memmove(base+0x200000,stack,len(stack))
 cpu=ctypes.create_string_buffer(rng.getrandbits(440*8).to_bytes(440,'little'),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);before=cpu.raw
 for reg,value in zip(regs,values):u.reg_write(reg,value)
 u.emu_start(0x556cff,0x556d06,count=8);assert u.reg_read(X.UC_X86_REG_EIP)==0x556d06
 target.value=0;lib.enter(cpu);assert target.value==0x556d06
 assert list(struct.unpack_from('<9I',cpu))==[u.reg_read(reg) for reg in regs],sample
 assert cpu.raw[36:]==before[36:],sample
 assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536)),sample
report=dict(passed=True,cases=512,variant_sha256=sha(out/'result.json'),llvm_sha256=sha(out/'variant.ll'),binary_sha256=sha(libpath),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Strict emitted replacement compared with original patched x86: full GPR/flags, remaining CPU state, 64KiB stack and continuation. Includes unaligned ESP and both DF states. No gameplay or integrated publication claim.')
(out/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: 512 native/x86 input teardown comparisons')
