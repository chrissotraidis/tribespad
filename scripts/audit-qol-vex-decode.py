#!/usr/bin/env python3
"""Inventory decoder gaps at recursively audited IFC22 VEX instruction starts."""
import argparse,collections,hashlib,json,pathlib,struct,subprocess
import capstone,pefile
ap=argparse.ArgumentParser();ap.add_argument('--include-nonvector',action='store_true');a=ap.parse_args()
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
image=R/'ref/product/IFC22.dll';analysis=R/'generated/analysis/ifc22';audit=json.loads((analysis/'audit.json').read_text());assert audit['sha256']==sha(image)
p=pefile.PE(str(image));base=p.OPTIONAL_HEADER.ImageBase;decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);records=[]
for (pc,) in struct.iter_unpack('<I',(analysis/'instructions.u32').read_bytes()):
    raw=p.get_data(pc-base,15)
    if raw[:1] not in (b'\xc4',b'\xc5'):continue
    instruction=next(decoder.disasm(raw,pc,count=1),None);assert instruction is not None
    if not a.include_nonvector and not instruction.mnemonic.startswith('v'):continue
    records.append({'pc':hex(pc),'bytes_hex':instruction.bytes.hex(),'mnemonic':instruction.mnemonic,'assembly':instruction.mnemonic+' '+instruction.op_str,'size':instruction.size})
U=R/'generated/tools/SRW-tribes/udis86-1.7.2';source=R/'tests/qol_vex_decode_audit.c';sources=[source]+sorted((U/'libudis86').glob('*.c'));out=R/'generated/sse'/('qol-all-vex-decode' if a.include_nonvector else 'qol-vex-decode');out.mkdir(parents=True,exist_ok=True);exe=out/'native'
subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-DHAVE_STRING_H=1','-I',str(U)]+list(map(str,sources))+['-o',str(exe)],check=True)
request=''.join(f"{r['pc']} {r['bytes_hex']}\n" for r in records);(out/'requests.txt').write_text(request)
run=subprocess.run([str(exe)],input=request,text=True,capture_output=True,check=True);(out/'replies.txt').write_text(run.stdout);lines=run.stdout.splitlines();assert len(lines)==len(records)
missing=[];counts=collections.Counter();decoded=collections.Counter()
for r,line in zip(records,lines):
    pc,length,mnemonic=line.split();assert int(pc,16)==int(r['pc'],16)
    if int(length)!=r['size'] or mnemonic!=r['mnemonic']:
        missing.append(dict(r,decoder_length=int(length),decoder_mnemonic=mnemonic));counts[r['mnemonic']]+=1
    else:decoded[r['mnemonic']]+=1
paths=sources+sorted((U/'libudis86').glob('*.h'))+[U/'udis86.h',pathlib.Path(__file__).resolve()]
report={'state':'finished','image_sha256':sha(image),'audit_sha256':sha(analysis/'audit.json'),'instruction_starts_sha256':sha(analysis/'instructions.u32'),'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'binary_sha256':sha(exe),'requests_sha256':sha(out/'requests.txt'),'replies_sha256':sha(out/'replies.txt'),'audited_vex_sites':len(records),'decoded_sites':len(records)-len(missing),'undecoded_sites':len(missing),'undecoded_families':dict(counts),'decoded_families':dict(decoded),'missing':missing,'records':records,'includes_nonvector_vex':a.include_nonvector,'scope':'Host finite decoder only at existing recursively audited instruction starts; inherited code/data heuristics are not execution coverage. Decoded sites do not prove backend lowering, runtime initialization, patch routing or gameplay. No EVEX/unprefixed legacy coverage claim.'}
(analysis/('all-vex-decode-gap.json' if a.include_nonvector else 'vex-decode-gap.json')).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['audited_vex_sites','decoded_sites','undecoded_sites','undecoded_families']},indent=2))
