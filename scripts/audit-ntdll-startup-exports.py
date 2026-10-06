#!/usr/bin/env python3
"""Bind the observed startup failure to the original required export group."""
import hashlib,json,pathlib,struct
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();source=R/'ref/product/IFC22.dll';pe=pefile.PE(str(source));base=pe.OPTIONAL_HEADER.ImageBase;md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
run_path=R/'generated/srw/run-20261002T170057-ifc22/native-diagnostic/checked/bootstrap-published/result.json';run=json.loads(run_path.read_text());assert run['initialized'] and run['late_patch_publication'] and run['result']['import']['name']=='DebugBreak'
ins=list(md.disasm(pe.get_data(0x87180,0xd5),base+0x87180));rows=[];name_pointer=None
for index,instruction in enumerate(ins):
 if instruction.mnemonic=='push' and instruction.operands[0].type==capstone.x86.X86_OP_IMM:name_pointer=instruction.operands[0].imm
 if instruction.mnemonic!='call' or instruction.op_str!='esi':continue
 name=pe.get_data(name_pointer-base,256).split(b'\0')[0].decode('ascii');following=ins[index+1:index+4]
 if len(following)!=3 or [i.mnemonic for i in following]!=['mov','test','je']:continue
 store,test,branch=following;assert store.operands[0].type==capstone.x86.X86_OP_MEM and store.op_str.endswith(', eax') and test.op_str=='eax, eax'
 slot=store.operands[0].mem.disp;error=branch.operands[0].imm;error_ins=list(md.disasm(pe.get_data(error-base,17),error));assert [i.mnemonic for i in error_ins]==['push','call','push','call']
 assert error_ins[-1].operands[0].imm==0x10080d90
 rows.append(dict(name=name,name_pointer=name_pointer,lookup_call=instruction.address,storage=slot,error_branch=error,error_return=error+17,instructions=[dict(pc=i.address,text=i.mnemonic+' '+i.op_str,bytes=i.bytes.hex()) for i in [instruction,*following,*error_ins]]))
assert len(rows)==8 and rows[0]['name']=='RtlNtStatusToDosError'
observed_return=run['result']['stack_arguments'][1];matches=[r for r in rows if r['error_return']==observed_return];assert len(matches)==1 and run['result']['stack_arguments'][2]==127
assert any(e['operation']=='GetModuleHandleA' and e['caller']==0x1008718f and e['name']=='ntdll.dll' and e['result']==0x20000000 for e in run['module_events'])
profile=json.loads((R/'config/runtime/native-providers.json').read_text());exports=next(p['exports'] for p in profile['providers'] if p['name']=='ntdll.dll');assert next(r for r in rows if r['name'] not in exports)==matches[0]
report=dict(verified=True,run_path=str(run_path.relative_to(R)),run_sha256=sha(run_path),dll_sha256=sha(source),provider_profile_sha256=sha(R/'config/runtime/native-providers.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),observed_missing=matches[0]['name'],required_exports=rows,scope='Original eight non-null export requirements and error branches verified from pinned x86. Actual native error helper return and code127 identify the first missing export. No NT service semantics, downstream calls or Windows parity established.')
out=R/'generated/integration-review/ntdll-startup-exports.json';out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(observed_missing=report['observed_missing'],required=[r['name'] for r in rows]),indent=2))
