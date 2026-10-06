#!/usr/bin/env python3
"""Bind the debugger's original CreateFileW request to the preserved executable."""
import hashlib,json,pathlib,struct
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/createfile-frontier'
frozen=R/'generated/integration-review/highbyte-compare-repair-checkpoint-20261003'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
checkpoint=json.loads((frozen/'checkpoint.json').read_text());assert checkpoint['passed']
for row in checkpoint['files']:
    if row['path'] in ['continuation/bootstrap','continuation/result.json']:
        assert sha(frozen/row['path'])==row['sha256']
run=json.loads((frozen/'continuation/result.json').read_text())
assert sha(frozen/'continuation/bootstrap')==run['binary_sha256']
request=json.loads((out/'request.json').read_text());pc=request['stack'][0]
original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original))
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
decoded=list(md.disasm(pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase-6,6),pc-6))
assert len(decoded)==1 and decoded[0].mnemonic=='call' and decoded[0].size==6
op=decoded[0].operands[0];assert op.type==capstone.x86.X86_OP_MEM and not op.mem.base and not op.mem.index
slot=op.mem.disp;imports={i.address:i.name.decode() for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name}
assert imports[slot]=='CreateFileW' and pc==0x1011e382
assert request['stack'][1:]==request['arguments'][1:]
assert (out/'name.bin').read_bytes().decode('utf-16le')=='public.store\0'
assert struct.unpack('<3I',(out/'security.bin').read_bytes())==(12,0,1)
cpu=(out/'cpu.bin').read_bytes();assert len(cpu)==440
assert struct.unpack_from('<Q',cpu,112)[0]==request['arguments'][0]
assert list(struct.unpack('<8I',(out/'stack.bin').read_bytes()))==request['stack']
report=dict(passed=True,binary_sha256=run['binary_sha256'],checkpoint_sha256=sha(frozen/'checkpoint.json'),
            native_report_sha256=sha(frozen/'continuation/result.json'),original_sha256=sha(original),
            caller_pc=pc-6,return_pc=pc,call_bytes=decoded[0].bytes.hex(),iat_slot=slot,
            api='CreateFileW',name='public.store',access=0x80000000,share=1,creation=3,flags=0x80,
            security_attributes=[12,0,1],template=0,
            inputs={n:sha(out/n) for n in ['request.json','cpu.bin','stack.bin','name.bin','security.bin','capture.lldb','capture-state.py','lldb.log']},
            script_sha256=sha(pathlib.Path(__file__).resolve()),
            scope='LLDB stops before the original unsupported runtime call. The seven emitted native arguments agree with the captured guest stack and original CreateFileW IAT call. No debugger-substituted result or runtime semantics.')
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: preserved original CreateFileW request and guest ABI verified')
