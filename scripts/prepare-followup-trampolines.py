#!/usr/bin/env python3
"""Strictly lift the verified captured trampoline bank into finite ARM64 code.

External original-image continuation/call targets stay unresolved. This object
is not published into the game until routing and execution checks pass.
"""
import argparse,hashlib,importlib.util,json,os,pathlib,struct,subprocess
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('plan',type=pathlib.Path);a=p.parse_args();plan_path=a.plan.resolve();plan=json.loads(plan_path.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert plan['verified'] and not plan['native_patch_publication'] and not plan['complete_phase'];assert plan['script_sha256']==sha(R/'scripts/audit-followup-bank.py')
assert sha(R/plan['trace_path'])==plan['trace_sha256'] and sha(R/plan['bank_path'])==plan['bank_sha256']
out=R/'generated/integration-review/followup-trampolines';out.mkdir(exist_ok=True);bank=plan['native_allocation']['address'];base=bank-65536
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True;targets=set();code=bytearray(b'\xcc'*65536)
for row in plan['trampolines']:
 raw=bytes.fromhex(row['code_hex']);offset=row['address']-bank;assert 0<=offset<65536-len(raw);code[offset:offset+len(raw)]=raw
 for ins in decoder.disasm(raw,row['address']):
  if ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):assert ins.operands[0].type==capstone.x86.X86_OP_IMM;targets.add(ins.operands[0].imm)
pages=sorted({target&~4095 for target in targets});assert all(page>=bank+65536 for page in pages)
original=R/'ref/product/IFC22.dll';raw=bytearray(original.read_bytes()[:0x400]);header=struct.unpack_from('<I',raw,0x3c)[0];optional=header+24;section=optional+224;raw[section:]=bytes(len(raw)-section);raw.extend(bytes(4096-len(raw)))
struct.pack_into('<H',raw,header+6,1+len(pages));struct.pack_into('<H',raw,header+22,0x102)
for offset,value in [(4,65536),(8,4096*len(pages)),(16,plan['trampolines'][0]['address']-base),(28,base),(56,pages[-1]+4096-base),(60,4096)]:struct.pack_into('<I',raw,optional+offset,value)
raw[optional+96:optional+224]=bytes(128);sections=[(bank,bytes(code),0x60000020)]+[(page,bytes(4096),0x40000040) for page in pages]
assert section+40*len(sections)<=4096
for i,(address,data,flags) in enumerate(sections):
 struct.pack_into('<8s6I2HI',raw,section+40*i,('.t%d'%i).encode(),len(data),address-base,len(data),len(raw),0,0,0,0,flags);raw.extend(data)
(out/'bank.exe').write_bytes(raw);(out/'relocations.csv').write_text('');(out/'global_aliases.sci').write_text(''.join('loc_%X,tbpatch_%08x\n'%(r['address'],r['address']) for r in plan['trampolines']));(out/'external_procedures.sci').write_text(''.join('loc_%X,tbpatch_target_%08x\n'%(x,x) for x in sorted(targets)))
pe=pefile.PE(data=bytes(raw))
for row in plan['trampolines']:assert pe.get_data(row['address']-base,len(bytes.fromhex(row['code_hex']))).hex()==row['code_hex']
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
run=subprocess.run([str(srw),'bank.exe','bank.llasm'],cwd=out,env={k:v for k,v in {**os.environ,'TRIBES_SRW_FULL_FLAGS':'1'}.items() if k!='HALOPAD_SRW_DIAG'},capture_output=True,text=True);(out/'srw.log').write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
(out/'extern.llinc').write_text(''.join('proc tbpatch_target_%08x external\n'%x for x in sorted(targets)));(out/'macros.llinc').write_text('')
spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
lines,stats=va.transform((out/'seg01_code.llinc').read_text().splitlines());(out/'native-code.llinc').write_text('\n'.join(lines)+'\n')
(out/'native.llasm').write_text('include llasm.llinc\ninclude extern.llinc\ninclude native-code.llinc\n')
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(out/'native.llasm')],cwd=out,check=True,capture_output=True)
metadata=va.postprocess(out/'raw.ll',out/'module.ll')
entries={row['address']:'tbpatch_%08x'%row['address'] for row in plan['trampolines']}
for name in metadata[0]:
 address=int(name[4:],16);assert bank<=address<bank+65536 and address not in entries;entries[address]=name
(out/'dispatch-entries.json').write_text(json.dumps([dict(address=address,symbol=symbol) for address,symbol in sorted(entries.items())],indent=2)+'\n')
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-c',str(out/'module.ll'),'-o',str(out/'module.o')],check=True)
undefined={line.strip().split()[-1] for line in subprocess.check_output(['nm','-u',str(out/'module.o')],text=True).splitlines() if line.strip()}
assert undefined=={'_tbpatch_target_%08x'%target for target in targets},undefined
result=dict(state='compiled_unpublished',plan_sha256=sha(plan_path),plan=str(plan_path.relative_to(R)),srw_sha256=sha(srw),object_sha256=sha(out/'module.o'),module_sha256=sha(out/'module.ll'),bank_sha256=sha(out/'bank.exe'),trampolines=len(plan['trampolines']),external_targets=sorted(targets),address_transform=stats,postprocess=metadata,script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Strict AOT translation of exact captured trampoline bytes. External original-image targets unresolved; no native execution equivalence or dispatch publication yet.')
result['dispatch_entries_sha256']=sha(out/'dispatch-entries.json');result['undefined_symbols']=sorted(undefined)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
