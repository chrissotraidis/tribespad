#!/usr/bin/env python3
"""Strictly lift historical entries whose patched bytes overlap another stream."""
import hashlib,importlib.util,json,os,pathlib,struct,subprocess
import pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
root=R/'generated/integration-review/string-patched-engine';parent=json.loads((root/'result.json').read_text())
assert parent['state']=='strictly_lifted_unpublished' and not parent['published']
plan=root/'overlap-entries.json';assert sha(plan)==parent['overlap_entries_sha256']
entries=json.loads(plan.read_text());assert entries==parent['overlap_entries']
image=root/'Tribes2.exe';assert sha(image)==parent['image_sha256'];source=pefile.PE(str(image))
base=0x430000;start=0x440000;code=bytearray(b'\xcc'*65536);targets=set()
for row in entries:
 data=bytes.fromhex(row['code_hex']);assert source.get_data(row['address']-0x400000,len(data))==data
 offset=row['address']-start;assert 0<=offset and offset+len(data)<=len(code);code[offset:offset+len(data)]=data;targets.update(row['targets'])
assert {row['address'] for row in entries}=={0x44b7c0,0x44b7c7}
assert targets=={0x44b7e2,0x44b7e8,0x44b7ed}
# SRW requires an explicit edge at a fallthrough block boundary. This
# compiler-only jump exits to the original next PC; it is outside every
# captured byte span and is never copied into guest memory.
synthetic_exit=0x44b7e2;code[synthetic_exit-start:synthetic_exit-start+5]=bytes.fromhex('e9fbffffff')
out=root/'overlaps';out.mkdir(exist_ok=True)
# A compiler-only PE holds exactly these two byte streams at their real VAs.
# Declared external boundaries stop decoding before the remaining engine code.
raw=bytearray((R/'ref/product/IFC22.dll').read_bytes()[:0x400]);header=struct.unpack_from('<I',raw,0x3c)[0];optional=header+24;section=optional+224
raw[section:]=bytes(len(raw)-section);raw.extend(bytes(4096-len(raw)))
struct.pack_into('<H',raw,header+6,1);struct.pack_into('<H',raw,header+22,0x102)
for offset,value in [(4,65536),(8,0),(16,entries[0]['address']-base),(28,base),(56,start+65536-base),(60,4096)]:struct.pack_into('<I',raw,optional+offset,value)
raw[optional+96:optional+224]=bytes(128)
struct.pack_into('<8s6I2HI',raw,section,b'.text',65536,start-base,65536,len(raw),0,0,0,0,0x60000020);raw.extend(code)
(out/'overlaps.exe').write_bytes(raw);(out/'relocations.csv').write_text('')
(out/'global_aliases.sci').write_text(''.join('loc_%X,loc_%X\n'%(r['address'],r['address']) for r in entries))
(out/'external_procedures.sci').write_text(''.join('loc_%X,loc_%X\n'%(v,v) for v in sorted(targets)))
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
env={k:v for k,v in os.environ.items() if k!='HALOPAD_SRW_DIAG'};env['TRIBES_SRW_FULL_FLAGS']='1'
run=subprocess.run([str(srw),'overlaps.exe','overlaps.llasm'],cwd=out,env=env,capture_output=True,text=True);(out/'srw.log').write_text(run.stdout+run.stderr);assert run.returncode==0,run.stderr
(out/'extern.llinc').write_text(''.join('proc loc_%X external\n'%v for v in sorted(targets)));(out/'macros.llinc').write_text('')
spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
lines,stats=va.transform((out/'seg01_code.llinc').read_text().splitlines());(out/'native-code.llinc').write_text('\n'.join(lines)+'\n')
(out/'native.llasm').write_text('include llasm.llinc\ninclude extern.llinc\ninclude native-code.llinc\n')
run=subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(out/'native.llasm')],cwd=out,capture_output=True,text=True);(out/'llasm.log').write_text(run.stdout+run.stderr);assert run.returncode==0,run.stderr
procedures,removed_wrappers=va.postprocess(out/'raw.ll',out/'module.ll')
assert set(procedures)=={'loc_44B7C0','loc_44B7C7','loc_44B7E0'},procedures
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-c',str(out/'module.ll'),'-o',str(out/'module.o')],check=True)
undefined={line.strip().split()[-1] for line in subprocess.check_output(['nm','-u',str(out/'module.o')],text=True).splitlines() if line.strip()}
assert undefined=={'_loc_%X'%v for v in targets},undefined
report=dict(state='compiled_unpublished',published=False,preparation_sha256=sha(root/'result.json'),plan_sha256=sha(plan),image_sha256=sha(image),module_sha256=sha(out/'module.ll'),object_sha256=sha(out/'module.o'),procedures=procedures,synthetic_fallthrough_exit=synthetic_exit,external_targets=sorted(targets),script_sha256=sha(pathlib.Path(__file__).resolve()),srw_sha256=sha(srw),scope='Exact historical entry streams, lifted separately because the main patch introduced overlapping instruction boundaries. A compiler-only synthetic jump delimits the fallthrough edge at44b7e2, outside the captured prefixes. Finite engine exits; no native equivalence or activation claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Strict overlap definitions:',procedures)
