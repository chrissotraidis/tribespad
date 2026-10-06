#!/usr/bin/env python3
"""Inventory EVEX instructions omitted by the separate C4/C5 decoder audit."""
import collections,hashlib,json,pathlib,struct
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];image=R/'ref/product/IFC22.dll';analysis=R/'generated/analysis/ifc22';starts=analysis/'instructions.u32';p=pefile.PE(str(image));decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);records=[]
for (pc,) in struct.iter_unpack('<I',starts.read_bytes()):
 raw=p.get_data(pc-p.OPTIONAL_HEADER.ImageBase,15)
 if raw[:1]!=b'\x62':continue
 ins=next(decoder.disasm(raw,pc,count=1),None)
 if ins is None or ins.mnemonic=='bound':continue
 records.append({'pc':hex(pc),'bytes_hex':ins.bytes.hex(),'mnemonic':ins.mnemonic,'assembly':ins.mnemonic+' '+ins.op_str,'size':ins.size})
sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
report={'image_sha256':sha(image),'instruction_starts_sha256':sha(starts),'script_sha256':sha(pathlib.Path(__file__)),'capstone_version':capstone.__version__,'sites':len(records),'families':dict(collections.Counter(r['mnemonic'] for r in records)),'records':records,'scope':'Capstone EVEX instruction census at unprefixed 62 starts in inherited recursive audit, excluding legacy BOUND. Not a Tribes decoder/backend test, native execution coverage or proof of optional-path reachability. Prefixed/unvisited instructions excluded.'}
(analysis/'evex-inventory.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['sites','families']},indent=2))
