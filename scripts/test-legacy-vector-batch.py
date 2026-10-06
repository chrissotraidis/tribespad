#!/usr/bin/env python3
"""Execute original and synthetic legacy vector bit operations as emitted ARM64."""
import hashlib
import json
import pathlib
import re
import subprocess

import capstone
import pefile
from sse_build_inputs import sse_inputs
from sse_instruction_fixture import fixture

R = pathlib.Path(__file__).resolve().parents[1]
out = R / 'generated/integration-review/vector-batch'
out.mkdir(parents=True, exist_ok=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
original = R / 'ref/product/IFC22.dll'
pe = pefile.PE(str(original))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
families = [('andpd',0,'660f54'),('orpd',1,'660f56'),('xorpd',2,'660f57'),
            ('andnpd',3,'660f55'),('unpcklpd',4,'660f14'),('unpckhpd',5,'660f15'),
            ('punpcklqdq',4,'660f6c'),('punpckhqdq',5,'660f6d'),
            ('movapd',6,'660f28'),('movdqa',6,'660f6f')]
operations = {name: op for name,op,_ in families}
records = []
registers = ['eax','ecx','edx','ebx','esp','ebp','esi','edi']
def add(raw, pc=None):
    i = next(md.disasm(raw,pc or 0))
    assert i.size == len(raw) and i.mnemonic in operations
    memory = next((p for p in i.operands if p.type == capstone.x86.X86_OP_MEM),None)
    store = i.operands[0].type == capstone.x86.X86_OP_MEM
    dest = int(i.reg_name(i.operands[0].reg)[3:]) if not store else 0
    src = int(i.reg_name(i.operands[1].reg)[3:]) if memory is None or store else 0
    base = index = -1; scale = disp = 0
    if memory:
        base = registers.index(i.reg_name(memory.mem.base)) if memory.mem.base else -1
        index = registers.index(i.reg_name(memory.mem.index)) if memory.mem.index else -1
        scale,disp = memory.mem.scale,memory.mem.disp & 0xffffffff
    records.append(dict(pc=pc,bytes_hex=raw.hex(),name=i.mnemonic,op=operations[i.mnemonic],
                        kind=2 if store else 1 if memory else 0,dest=dest,source=src,
                        base=base,index=index,scale=scale,disp=disp))
baseline=R/'generated/integration-review/region-checkpoint-20261002/generated/integration-review/lifter-gaps.json'
gaps=json.loads(baseline.read_text())
assert gaps['source_sha256'][gaps['lift']+'/IFC22.dll']==sha(original)
for row in gaps['sites']:
    if row['mnemonic'] in operations:
        pc=int(row['pc'],16);raw=bytes.fromhex(row['bytes_hex'])
        assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,len(raw))==raw
        add(raw,pc)
original_count=len(records)
for name,op,prefix in families:
    prefixes=[prefix]+(['660f29'] if name=='movapd' else ['660f7f'] if name=='movdqa' else [])
    for prefix in prefixes:
        for d in range(8):
            for s in range(8):add(bytes.fromhex(prefix)+bytes([0xc0|d<<3|s]))
            for tail in [bytes([d<<3]),bytes([0x44|d<<3,0x24,0x10]),
                         bytes([0x44|d<<3,0xb3,0xf0]),bytes([5|d<<3])+bytes.fromhex('10002000')]:
                add(bytes.fromhex(prefix)+tail)
srw=R/'generated/tools/SRW-tribes/SRW.exe'
build=json.loads((srw.parent/'tribes-build.json').read_text())
assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
run=fixture(out/'fixture',records,original,srw)
assert run.returncode==0,run.stderr
negative=[]
for name,_,prefix in families:
    for kind,pfx in [('address16','67'),('segment','64'),('lock','f0')]:
        run=fixture(out/('negative-'+name+'-'+kind),[dict(bytes_hex=pfx+prefix+'00')],original,srw)
        assert run.returncode!=0,(name,kind)
        negative.append(dict(name=name,kind=kind,exit=run.returncode))
text=(out/'fixture/seg01_code.llinc').read_text()
procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S))
assert len(procedures)==len(records)
source=out/'bodies.llasm'
source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures))
support=R/'port/llasm-support';ir=out/'bodies.ll';llasm=R/'generated/tools/llasm/llasm'
subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
keys=['op','kind','dest','source','base','index','scale','disp']
(out/'moves.inc').write_text('typedef struct {unsigned op,kind,dest,source;int base,index;unsigned scale,disp;} move_case;\n'+
    ''.join('extern void move_%d(_cpu *);\n'%i for i in range(len(records)))+
    'static void (*const bodies[])(_cpu *)={'+','.join('move_%d'%i for i in range(len(records)))+'};\n'+
    'static const move_case moves[]={'+','.join('{'+','.join(str(v[k])+('u' if k=='disp' else '') for k in keys)+'}' for v in records)+'};\n')
math=sse_inputs('macos');exe=out/'native';test=R/'tests/legacy_vector_batch_test.c'
subprocess.run(['clang','-arch','arm64','-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror',
                '-Wno-override-module','-I',str(out)]+math['compile_flags']+
               [str(ir),str(test),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120)
assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
(out/'run.log').write_text(run.stdout)
paths=[test,pathlib.Path(__file__).resolve(),support/'tribes_sse.c',support/'tribes_sse.h',support/'tribes_sse.llinc',support/'llasm_cpu.h']
report=dict(pass_=True,original_sites=original_count,blocks=len(records),cases=len(records)*256,
            negative=negative,records=records,output=run.stdout.strip(),patch_sha256=build['patch_sha256'],
            baseline_gap_report_sha256=sha(baseline),
            srw_sha256=sha(srw),binary_sha256=sha(exe),source_sha256={str(p.relative_to(R)):sha(p) for p in paths},
            sse_math=math['evidence'],scope='Emitted ARM64 host batch: original sites/all register aliases/four memory forms, independent bit-pattern expectations/full440CPU, protected memory and fault tests. No simulator batch/physical device or native DLL/startup/gameplay acceptance.')
report['pass']=report.pop('pass_')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(run.stdout.strip());print('Original sites:',original_count,'blocks:',len(records))
