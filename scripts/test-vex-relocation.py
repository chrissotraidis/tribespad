#!/usr/bin/env python3
"""Reproduce third-source relocation validation using unchanged original bytes."""
import argparse,hashlib,json,os,pathlib,struct,subprocess
import pefile
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--expect-rejection',action='store_true');ap.add_argument('--preview',action='store_true');a=ap.parse_args()
D=R/('generated/sse/vex-relocation-preview' if a.preview else 'generated/sse/vex-relocation');D.mkdir(parents=True,exist_ok=True)
original=R/'ref/product/IFC22.dll';raw=bytearray(original.read_bytes()[:0x400]);p=pefile.PE(str(original));code=bytes.fromhex('c5f85705a0b11c10')
assert p.get_data(0xcaa1,len(code))==code
header=struct.unpack_from('<I',raw,0x3c)[0];optional=header+24;section=optional+224
struct.pack_into('<H',raw,header+6,2);struct.pack_into('<H',raw,header+22,0x103)
for off,value in [(4,512),(8,512),(16,0x1000),(56,0x1cc000),(60,0x400)]:struct.pack_into('<I',raw,optional+off,value)
raw[optional+96:optional+224]=bytes(128);raw[section:]=bytes(len(raw)-section)
struct.pack_into('<8s6I2HI',raw,section,b'.text\0\0\0',len(code)+1,0x1000,512,0x400,0,0,0,0,0x60000020)
struct.pack_into('<8s6I2HI',raw,section+40,b'.rdata\0\0',512,0x1cb000,512,0x600,0,0,0,0,0x40000040)
raw+=code+b'\xc3'+bytes(511-len(code))+bytes(512)
(D/'fixture.exe').write_bytes(raw);(D/'relocations.csv').write_text('0x10001004,0x101cb1a0,\n');(D/'global_aliases.sci').write_text('loc_10001000,fixture_0\n')
pe=pefile.PE(data=bytes(raw));assert pe.get_data(0x1000,len(code))==code and pe.get_data(0x1cb1a0,16)==bytes(16)
srw=R/('generated/tools/SRW-relocation-preview/SRW.exe' if a.preview else 'generated/tools/SRW-tribes/SRW.exe')
build=json.loads((srw.parent/'tribes-build.json').read_text());assert hashlib.sha256(srw.read_bytes()).hexdigest()==build['srw_sha256']
run=subprocess.run([str(srw),'fixture.exe','fixture.llasm'],cwd=D,env={k:v for k,v in {**os.environ,'TRIBES_SRW_FULL_FLAGS':'1'}.items() if k!='HALOPAD_SRW_DIAG'},capture_output=True,text=True,timeout=60)
(D/'stdout.log').write_text(run.stdout);(D/'stderr.log').write_text(run.stderr)
if a.expect_rejection:assert run.returncode!=0 and 'Error: -51' in run.stderr and 'op type: 156 - 156 - 157' in run.stdout
else:assert run.returncode==0,(run.stdout,run.stderr)
report={'pass':True,'expected_rejection':a.expect_rejection,'exit':run.returncode,'original_pc':'0x1000caa1','original_bytes_hex':code.hex(),'fixture_sha256':hashlib.sha256(raw).hexdigest(),'srw_sha256':build['srw_sha256'],'patch_sha256':build['patch_sha256'],'source_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'scope':'Strict third-source relocation validation fixture; native execution checked separately.'}
(D/('before-fix.json' if a.expect_rejection else 'result.json')).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
