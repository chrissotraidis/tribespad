#!/usr/bin/env python3
"""Strictly lift the captured NOP-patched engine entries at their real VAs."""
import hashlib,importlib.util,json,os,pathlib,re,struct,subprocess
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen=R/'generated/integration-review/post-video-prefix-checkpoint-20261003';cap=frozen/'generated/integration-review/post-video-nop';out=R/'generated/integration-review/post-video-nop-candidate';out.mkdir(exist_ok=True)
checkpoint=json.loads((frozen/'checkpoint.json').read_text());analysis=json.loads((cap/'analysis.json').read_text());assert analysis['passed'] and not analysis['published'];assert sha(cap/'analysis.json')==checkpoint['files'][str((cap/'analysis.json').relative_to(frozen))]
rawcode=bytes.fromhex(analysis['after_bytes']);assert len(rawcode)==0x36 and rawcode[3:30]==b'\x90'*27
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';text=engine.read_text();names=['loc_4025E2','loc_4025FD'];old={n:definition(text,n) for n in names}
# Preserve every historical native entry within the changed span, even though
# NOPs no longer create an original call-return boundary there.
entries=[int(n,16) for n in re.findall(r'^define (?:hidden|protected) fastcc void @loc_([0-9A-F]+)\(',text,re.M) if 0x4025e2<=int(n,16)<0x402618];assert entries==[0x4025e2,0x4025fd],entries
code=bytearray(b'\xcc'*0x25000);code[0x5e2:0x618]=rawcode
header_image=R/'ref/product/IFC22.dll';raw=bytearray(header_image.read_bytes()[:0x400]);h=struct.unpack_from('<I',raw,0x3c)[0];optional=h+24;section=optional+224;raw[section:]=bytes(len(raw)-section);raw.extend(bytes(4096-len(raw)))
struct.pack_into('<H',raw,h+6,1);struct.pack_into('<H',raw,h+22,0x102)
for offset,value in [(4,len(code)),(8,0),(16,0x25e2),(28,0x400000),(56,0x27000),(60,4096)]:struct.pack_into('<I',raw,optional+offset,value)
raw[optional+96:optional+224]=bytes(128);struct.pack_into('<8s6I2HI',raw,section,b'.text',len(code),0x2000,len(code),len(raw),0,0,0,0,0x60000020);raw.extend(code)
(out/'candidate.exe').write_bytes(raw);(out/'relocations.csv').write_text('');(out/'global_aliases.sci').write_text(''.join('loc_%X,loc_%X\n'%(n,n) for n in entries));targets=[0x426650,0x402618];(out/'external_procedures.sci').write_text(''.join('loc_%X,loc_%X\n'%(n,n) for n in targets))
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
env={k:v for k,v in os.environ.items() if k!='HALOPAD_SRW_DIAG'};env['TRIBES_SRW_FULL_FLAGS']='1';run=subprocess.run([str(srw),'candidate.exe','candidate.llasm'],cwd=out,env=env,capture_output=True,text=True);(out/'srw.log').write_text(run.stdout+run.stderr);assert run.returncode==0,run.stderr
(out/'extern.llinc').write_text(''.join('proc loc_%X external\n'%n for n in targets));(out/'macros.llinc').write_text('');spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
# SRW follows the call-return boundary into compiler-only INT3 padding.
# Keep precisely the two historical entries covered by captured game bytes;
# the return address remains a guest constant, never a fixture procedure.
lifted=(out/'seg01_code.llinc').read_text();bounded=[]
for name in names:
 matches=re.findall(r'^proc '+name+r'[^\n]*\n.*?^endp\s*$',lifted,re.M|re.S)
 assert len(matches)==1,(name,len(matches))
 bounded.append(matches[0])
bounded='\n'.join(bounded)+'\n';assert 'INT 3' not in bounded
assert re.findall(r'^tcall (\w+)',bounded,re.M)==['loc_4025FD','loc_426650']
assert bounded.count('PUSH loc_402618')==1
lines,stats=va.transform(bounded.splitlines());(out/'native-code.llinc').write_text('\n'.join(lines)+'\n');(out/'native.llasm').write_text('include llasm.llinc\ninclude extern.llinc\ninclude native-code.llinc\n')
run=subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(out/'native.llasm')],cwd=out,capture_output=True,text=True);(out/'llasm.log').write_text(run.stdout+run.stderr);assert run.returncode==0,run.stderr
procedures,_=va.postprocess(out/'raw.ll',out/'engine.ll');assert set(procedures)==set(names),procedures
s=(out/'engine.ll').read_text();assert 'halopad_trap_unimplemented' not in s
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-c',str(out/'engine.ll'),'-o',str(out/'engine.o')],check=True)
undefined={line.strip().split()[-1] for line in subprocess.check_output(['nm','-u',str(out/'engine.o')],text=True).splitlines() if line.strip()};assert undefined=={'_loc_426650'},undefined
records=[]
for name,entry in zip(names,entries):
 records.append(dict(name=name,entry=entry,end=0x402618,target=0x426650,before_bytes=bytes.fromhex(analysis['before_bytes'])[entry-0x4025e2:].hex(),after_bytes=rawcode[entry-0x4025e2:].hex(),old_definition_sha256=hashlib.sha256(old[name].encode()).hexdigest(),new_definition_sha256=hashlib.sha256(definition(s,name).encode()).hexdigest()))
report=dict(passed=True,published=False,records=records,writes=analysis['writes'],entries=entries,target=0x426650,srw_sha256=sha(srw),inputs_sha256={str(p.relative_to(R)):sha(p) for p in [engine,frozen/'checkpoint.json',cap/'analysis.json',header_image,R/'scripts/prepare-va-model.py',R/'scripts/patches/srw-tribes.patch',pathlib.Path(__file__).resolve()]},outputs_sha256={n:sha(out/n) for n in ['candidate.exe','engine.ll','engine.o','native-code.llinc','srw.log','llasm.log']},scope='Compiler-only PE with exact patched bytes at original VAs, strict lowering and both historical entries retained. Removes only27-byte registration sequence; remaining pushes/CALL/return preserved. No native store admission or15th revision.')
(out/'preparation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: strict NOP variants',procedures,'; only native external target0x426650; unpublished')
