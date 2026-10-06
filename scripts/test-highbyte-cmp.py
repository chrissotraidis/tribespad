#!/usr/bin/env python3
"""Strict emitted memory/high-byte CMP, exhaustive native and original-x86 checks."""
import ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import pefile,unicorn
from unicorn import x86_const as X
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/highbyte-compare-repair/test';out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
original=R/'ref/product/IFC22.dll';assert pefile.PE(str(original)).get_data(0x115878,2)==bytes.fromhex('382f')
records=[dict(bytes_hex='382f',pc=0x10115878,source=1,shift=8,base=7,index=-1,scale=1,displacement=0)]
for src in range(4):
    reg=(src+4)<<3
    forms=[(bytes([reg|base]),base,-1,1,0) for base in [0,1,2,3,7]]
    forms += [(bytes([reg|0x44,0x24,0x10]),4,-1,1,16),(bytes([reg|0x45,0xfd]),5,-1,1,-3),(bytes([reg|0x44,0x83,0xef]),3,0,4,-17)]
    for raw,base,index,scale,disp in forms:records.append(dict(bytes_hex=(b'\x38'+raw).hex(),pc=None,source=src,shift=8,base=base,index=index,scale=scale,displacement=disp))
for src in range(4):records.append(dict(bytes_hex=bytes([0x38,src<<3|7]).hex(),pc=None,source=src,shift=0,base=7,index=-1,scale=1,displacement=0))
assert len(records)==37
run=fixture(out/'fixture',records,original,srw);assert run.returncode==0,run.stderr
negative=[]
for name,raw in [('address16','67382f'),('fs','64382f'),('gs','65382f'),('lock','f0382f'),('rep','f3382f'),('repne','f2382f')]:
    rejected=fixture(out/('negative-'+name),[dict(bytes_hex=raw)],original,srw)
    assert rejected.returncode!=0,(name,rejected.stdout,rejected.stderr)
    negative.append(dict(name=name,bytes_hex=raw,exit=rejected.returncode))
text=(out/'fixture/seg01_code.llinc').read_text();matches=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(matches)==len(records) and 'halopad_trap_unimplemented' not in text
source=out/'bodies.llasm';source.write_text('include llasm.llinc\n'+''.join('proc compare_%s public\n%sendp\n'%(m[1],m[2].split(';ret\n')[0]) for m in matches))
llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll'
subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(ir),str(source)],check=True)
(out/'cases.inc').write_text('typedef struct {unsigned source,shift;int base,index;unsigned scale;int displacement;} Case;\n'+''.join('extern void exported_compare_%d(_cpu *);\n'%i for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('exported_compare_%d'%i for i in range(len(records)))+'};\nstatic const Case cases[]={'+','.join('{'+','.join(str(r[k]) for k in ['source','shift','base','index','scale','displacement'])+'}' for r in records)+'};\n')
c=R/'tests/highbyte_cmp_test.c';exe=out/'native';libpath=out/'native.dylib'
flags=['clang','-arch','arm64','-O2','-Wno-override-module','-DPTROFS_64BIT']
bridge=out/'exports.ll';bridge.write_text('\n'.join('declare hidden fastcc void @compare_%d(ptr)\ndefine void @exported_compare_%d(ptr %%cpu) { call fastcc void @compare_%d(ptr %%cpu)\n ret void\n}'%(i,i,i) for i in range(len(records))))
subprocess.run(flags+['-Wall','-Wextra','-Werror','-I',str(out),str(ir),str(bridge),str(c),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/'native.log').write_text(run.stdout+run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:'),(run.returncode,run.stdout,run.stderr)
subprocess.run(flags+['-dynamiclib',str(ir),str(bridge),'-o',str(libpath)],check=True);lib=ctypes.CDLL(str(libpath))
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x10000,4096);u.mem_map(0x200000,0x20000)
memory=ctypes.create_string_buffer(0x20000);base=ctypes.addressof(memory)-0x200000
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];rng=random.Random(0x382f);count=0
for i,r in enumerate(records):
    fn=getattr(lib,'exported_compare_%d'%i);fn.argtypes=[ctypes.c_void_p];raw=bytes.fromhex(r['bytes_hex'])
    for sample in range(256):
        values=[0x204000+j*0x100 for j in range(8)]+[0x202|sum(bit for j,bit in enumerate([1,4,16,64,128,2048]) if sample&(1<<j))]
        # Keep real x86 addressing mapped while varying the selected high lane.
        # Cases whose source aliases a base/index deliberately exercise that dependency.
        if r['shift']==8:values[r['source']]=0x200000|(sample<<8)|rng.randrange(256)
        else:values[r['source']]=(values[r['source']]&~255)|sample
        if r['index']>=0 and r['index']!=r['source']:values[r['index']]=rng.randrange(16)
        if r['index']>=0 and r['index']==r['source']:values[r['base']]=(0x208000-values[r['index']]*r['scale'])&0xffffffff
        address=(values[r['base']]+(values[r['index']]*r['scale'] if r['index']>=0 else 0)+r['displacement'])&0xffffffff
        assert 0x200000<=address<0x220000
        value=sample if sample%3==0 else rng.randrange(256);data=bytes([value]);u.mem_write(address,data);ctypes.memmove(base+address,data,1)
        cpu=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);expected=bytearray(cpu.raw)
        for reg,v in zip(regs,values):u.reg_write(reg,v)
        u.mem_write(0x10000,raw);u.emu_start(0x10000,0x10000+len(raw),count=1);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs])
        fn(cpu);assert cpu.raw==expected,(i,sample,[(j,cpu.raw[j],expected[j]) for j in range(440) if cpu.raw[j]!=expected[j]])
        assert ctypes.string_at(base+address,1)==data==bytes(u.mem_read(address,1));count+=1
# Restore the real captured input for the exact original instruction.
frozen=R/'generated/integration-review/highbyte-compare-checkpoint-20261003';manifest=json.loads((frozen/'capture/instruction-pages.json').read_text());saved=(frozen/'capture/instruction-cpu.bin').read_bytes();edi=struct.unpack_from('<I',saved,28)[0]
row=next(r for r in manifest['ranges'] if r['address']<=edi<r['address']+r['size']);byte=(frozen/'capture'/row['file']).read_bytes()[edi-row['address']]
data=ctypes.create_string_buffer(bytes([byte]),1);cpu=ctypes.create_string_buffer(saved,440);struct.pack_into('<Q',cpu,112,ctypes.addressof(data)-edi);expected=bytearray((frozen/'analysis/reference-cpu.bin').read_bytes());struct.pack_into('<Q',expected,112,ctypes.addressof(data)-edi)
lib.exported_compare_0.argtypes=[ctypes.c_void_p];lib.exported_compare_0(cpu);assert cpu.raw==expected and data.raw==bytes([byte])
paths=[pathlib.Path(__file__).resolve(),c,R/'scripts/sse_instruction_fixture.py',R/'scripts/patches/srw-tribes.patch',R/'port/llasm-support/llasm_cpu.h']
report=dict(passed=True,blocks=len(records),exhaustive_cases=len(records)*65536,x86_cases=count,captured_native_case=True,boundary_reads=len(records)*2,records=records,negative=negative,
    srw_sha256=sha(srw),patch_sha256=build['patch_sha256'],llasm_sha256=sha(llasm),original_sha256=sha(original),capture_checkpoint_sha256=sha(frozen/'checkpoint.json'),
    sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},outputs_sha256={n:sha(out/n) for n in ['bodies.llasm','bodies.ll','cases.inc','native','native.dylib','exports.ll','native.log','fixture/fixture.exe','fixture/seg01_code.llinc']},
    scope='All65536 byte pairs across AH/CH/DH/BH and eight memory addressing forms, plus four low-byte controls and the actual site. Full440CPU/memory preservation, exact single-byte boundary reads,9472 original-x86 comparisons and captured native input. Host instruction lowering only; full DLL diff/startup checked separately.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip());print('PASS:',count,'original-x86 cases and exact captured native input')
