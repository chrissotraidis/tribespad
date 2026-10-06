#!/usr/bin/env python3
"""Record the selected original QoL handler; this does not execute it."""
import hashlib,json,pathlib
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];source=R/'ref/product/IFC22.dll';blob=source.read_bytes();digest=hashlib.sha256(blob).hexdigest();assert digest=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
pe=pefile.PE(data=blob);base=pe.OPTIONAL_HEADER.ImageBase;assert base==0x10000000
entry=0x1005fbe0;raw=pe.get_memory_mapped_image()[entry-base:entry-base+59];decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);instructions=[{'pc':i.address,'bytes_hex':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str} for i in decoder.disasm(raw,entry)]
assert instructions[0]['mnemonic']=='push' and instructions[-1]['pc']==0x1005fc18 and instructions[-1]['mnemonic']=='ret' and instructions[-1]['operands']=='4'
assert [i['operands'] for i in instructions if i['mnemonic']=='sub']==['eax, 1','eax, 1','eax, 4']
assert [i['operands'] for i in instructions if i['mnemonic']=='je']==['0x1005fbfb']*3
report={'state':'finished','qol_sha256':digest,'script_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'handler':entry,'body_sha256':hashlib.sha256(raw).hexdigest(),'instructions':instructions,'inferred_behavior':{'handled_events':[1,2,6],'unhandled_events':[0,5],'handled_path':'Load pointer at engine 0x9e8294, write zero at pointer+8, set QoL byte 0x1046ce6a and engine byte 0x9ecf23 to one, return TRUE','unhandled_path':'Return FALSE with stdcall argument cleanup'},'scope':'Static original-x86 branch/store audit only; no native QoL callback execution, initialized engine pointer, shutdown integration or completed game session claim'}
(R/'generated/qol-console-handler.json').write_text(json.dumps(report,indent=2)+'\n');print('Verified original console handler branch/store audit; events 1/2/6 handled, 0/5 declined')
