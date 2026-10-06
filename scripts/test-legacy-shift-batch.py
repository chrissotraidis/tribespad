#!/usr/bin/env python3
"""Execute original and synthetic legacy packed shifts as emitted ARM64."""
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
out = R / 'generated/integration-review/shift-batch'
out.mkdir(parents=True, exist_ok=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
original = R / 'ref/product/IFC22.dll'
pe = pefile.PE(str(original))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
families = [('psllw',0,0xf1,0x71,6),('pslld',1,0xf2,0x72,6),('psllq',2,0xf3,0x73,6),
            ('psrlw',3,0xd1,0x71,2),('psrld',4,0xd2,0x72,2),('psrlq',5,0xd3,0x73,2),
            ('psraw',6,0xe1,0x71,4),('psrad',7,0xe2,0x72,4),('pslldq',8,None,0x73,7)]
operations = {name: op for name,op,*_ in families}
records = []
registers = ['eax','ecx','edx','ebx','esp','ebp','esi','edi']
def add(raw, pc=None):
    i = next(md.disasm(raw,pc or 0))
    assert i.size == len(raw) and i.mnemonic in operations
    memory = next((p for p in i.operands if p.type == capstone.x86.X86_OP_MEM),None)
    immediate = i.operands[1].type == capstone.x86.X86_OP_IMM
    dest = int(i.reg_name(i.operands[0].reg)[3:])
    src = int(i.reg_name(i.operands[1].reg)[3:]) if not memory and not immediate else 0
    count = i.operands[1].imm if immediate else 0
    base = index = -1; scale = disp = 0
    if memory:
        base = registers.index(i.reg_name(memory.mem.base)) if memory.mem.base else -1
        index = registers.index(i.reg_name(memory.mem.index)) if memory.mem.index else -1
        scale,disp = memory.mem.scale,memory.mem.disp & 0xffffffff
    records.append(dict(pc=pc,bytes_hex=raw.hex(),name=i.mnemonic,op=operations[i.mnemonic],
                        kind=0 if immediate else 2 if memory else 1,dest=dest,source=src,count=count,
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
for name,op,opcode,group,extension in families:
    for dest in range(8):
        for count in ([0,1,15,16,31,32,63,64,255] if dest else range(256)):
            add(bytes([0x66,0x0f,group,0xc0|extension<<3|dest,count]))
        if opcode is not None:
            for source in range(8):add(bytes([0x66,0x0f,opcode,0xc0|dest<<3|source]))
            for tail in [bytes([dest<<3]),bytes([0x44|dest<<3,0x24,0x10]),
                         bytes([0x44|dest<<3,0xb3,0xf0]),bytes([5|dest<<3])+bytes.fromhex('10002000')]:
                add(bytes([0x66,0x0f,opcode])+tail)
srw=R/'generated/tools/SRW-tribes/SRW.exe'
build=json.loads((srw.parent/'tribes-build.json').read_text())
assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
run=fixture(out/'fixture',records,original,srw)
assert run.returncode==0,run.stderr
negative=[]
for name,_,opcode,group,extension in families:
    raw=bytes([0x66,0x0f,group,0xc0|extension<<3,1]).hex()
    for kind,pfx in [('address16','67'),('segment','64'),('lock','f0')]:
        if kind=='segment' and opcode is None:continue  # no memory operand; prefix is ignored by x86
        raw=bytes([0x66,0x0f,opcode,0]).hex() if opcode is not None else bytes([0x66,0x0f,group,0xc0|extension<<3,1]).hex()
        run=fixture(out/('negative-'+name+'-'+kind),[dict(bytes_hex=pfx+raw)],original,srw)
        assert run.returncode!=0,(name,kind)
        negative.append(dict(name=name,kind=kind,exit=run.returncode))
    raw=bytes([0x66,0x0f,group,extension<<3,1]).hex()
    run=fixture(out/('negative-'+name+'-immediate-memory'),[dict(bytes_hex=raw)],original,srw)
    assert run.returncode!=0,(name,'immediate-memory')
    negative.append(dict(name=name,kind='immediate-memory',exit=run.returncode))
text=(out/'fixture/seg01_code.llinc').read_text()
procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S))
assert len(procedures)==len(records)
source=out/'bodies.llasm'
source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures))
support=R/'port/llasm-support';ir=out/'bodies.ll';llasm=R/'generated/tools/llasm/llasm'
subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
keys=['op','kind','dest','source','count','base','index','scale','disp']
(out/'moves.inc').write_text('typedef struct {unsigned op,kind,dest,source,count;int base,index;unsigned scale,disp;} move_case;\n'+
    ''.join('extern void move_%d(_cpu *);\n'%i for i in range(len(records)))+
    'static void (*const bodies[])(_cpu *)={'+','.join('move_%d'%i for i in range(len(records)))+'};\n'+
    'static const move_case moves[]={'+','.join('{'+','.join(str(v[k])+('u' if k=='disp' else '') for k in keys)+'}' for v in records)+'};\n')
math=sse_inputs('macos');exe=out/'native';test=R/'tests/legacy_shift_batch_test.c'
reference=out/'reference-x86'
subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-msse2','-mno-avx','-DREFERENCE_X86','-Wall','-Wextra','-Werror',str(test),'-o',str(reference)],check=True)
oracle=subprocess.run([str(reference)],capture_output=True,text=True,timeout=60)
assert oracle.returncode==0 and oracle.stdout.startswith('PASS:') and not oracle.stderr,(oracle.returncode,oracle.stdout,oracle.stderr)
(out/'reference.log').write_text(oracle.stdout)
subprocess.run(['clang','-arch','arm64','-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror',
                '-Wno-override-module','-I',str(out)]+math['compile_flags']+
               [str(ir),str(test),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120)
assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
(out/'run.log').write_text(run.stdout)
paths=[test,pathlib.Path(__file__).resolve(),support/'tribes_sse.c',support/'tribes_sse.h',support/'tribes_sse.llinc',support/'llasm_cpu.h']
report=dict(pass_=True,original_sites=original_count,blocks=len(records),cases=len(records)*64,reference_output=oracle.stdout.strip(),reference_sha256=sha(reference),
            negative=negative,records=records,output=run.stdout.strip(),patch_sha256=build['patch_sha256'],
            baseline_gap_report_sha256=sha(baseline),
            srw_sha256=sha(srw),binary_sha256=sha(exe),source_sha256={str(p.relative_to(R)):sha(p) for p in paths},
            sse_math=math['evidence'],scope='Emitted ARM64 host batch: original sites/all register aliases/four memory forms, independent per-bit model checked against Rosetta SSE2/full440CPU, protected memory and fault tests. No simulator batch/physical device or native DLL/startup/gameplay acceptance.')
report['pass']=report.pop('pass_')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(oracle.stdout.strip());print(run.stdout.strip());print('Original sites:',original_count,'blocks:',len(records))
