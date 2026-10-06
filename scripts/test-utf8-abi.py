#!/usr/bin/env python3
"""Actual ARM64 UTF-8 wrappers against CPython Unicode codec expectations."""
import argparse,hashlib,importlib.util,json,pathlib,random,struct,subprocess
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();out=R/'generated/native-win32/utf8-abi'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"state":"running","pass":false}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({k:bindings[k] for k in ['MultiByteToWideChar','WideCharToMultiByte','GetCPInfo','IsValidCodePage']}))
blob=bytearray();rows=[];rng=random.Random(65401)
def save(raw):
 at=len(blob);blob.extend(raw);blob.extend(b'\x79'*3);return 0x200000+at

def add(api,raw,flags,count=None):
 codec='utf-8' if api==1 else 'utf-16-le';strict=bool(flags);error=123
 try:want=raw.decode(codec,'strict' if strict else 'replace').encode('utf-16-le' if api==1 else 'utf-8')
 except UnicodeDecodeError:want=b'';error=1113
 logical=count if count is not None else len(raw)//(2 if api==2 else 1);rows.append((api,flags,save(raw),logical,save(want),len(want)//(2 if api==1 else 1),error,len(raw),len(want)))
# Explicit boundaries, noncharacters, overlongs, surrogates, invalid second-byte
# ranges, illegal continuations and truncated maximal subparts, plus fixed-seed
# random byte strings. Expected replacement/strict behavior uses an independent
# Python codec, never native conversion output.
byte_cases=[bytes([i]) for i in range(256)]+[bytes.fromhex(s) for s in ['c080','c1bf','e08080','eda080','edbfbf','f0808080','f4908080','f5808080','fffe','e18041','f1808041','e180','f18080','efbbbf','efbfbf','f48fbfbf','f0908080','61c2a2e282acf09f98800062']]+[bytes(rng.randrange(256) for _ in range(rng.randrange(1,33))) for _ in range(128)]
for raw in byte_cases:
 for flag in [0,8]:add(1,raw,flag)
wide_cases=[struct.pack('<H',i) for i in [0,0x7f,0x80,0x7ff,0x800,0xd7ff,0xd800,0xdbff,0xdc00,0xdfff,0xe000,0xfffd,0xffff]]+[struct.pack('<'+'H'*len(v),*v) for v in [(0xd800,0xdc00),(0xdbff,0xdfff),(0xd800,65),(0xdc00,65),(0xd800,0xd800,0xdc00),(0xdc00,0xd800),(0xdbff,0xdfff,0,66)]]+[struct.pack('<16H',*[rng.randrange(65536) for _ in range(16)]) for _ in range(64)]
for raw in wide_cases:
 for flag in [0,0x80]:add(2,raw,flag)
for api,raw in [(1,'Tribes 😀\0'.encode()),(2,'Tribes 😀\0'.encode('utf-16-le'))]:
 for flag in ([0,8] if api==1 else [0,0x80]):add(api,raw,flag,0xffffffff)
# Exhaust all 1,112,064 Unicode scalar values in bounded streams. Noncharacters
# are legal scalars and are retained. Surrogates are tested separately above.
scalar_count=0
for start in range(0,0x110000,65536):
 text=''.join(chr(v) for v in range(start,min(start+65536,0x110000)) if not 0xd800<=v<=0xdfff);scalar_count+=len(text)
 for flag in [0,8]:add(1,text.encode('utf-8'),flag)
 for flag in [0,0x80]:add(2,text.encode('utf-16-le'),flag)
assert scalar_count==1112064 and len(blob)<0x4000000
fixture=out/'utf8_cases.bin';fixture.write_bytes(blob);case_header=out/'utf8_cases.h';case_header.write_text('/* Independent CPython Unicode codec oracle; all scalars and malformed inputs. */\nstatic const struct Case cases[]={\n'+''.join(' {'+','.join('0x%xu'%v for v in row)+'},\n' for row in rows)+'};\n')
sources=[R/'port/core/guest_encoding.c',R/'port/core/guest_heap.c',R/'tests/utf8_abi_test.c',R/'tests/fixtures/native_enter.ll',wrapper];runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 c=subprocess.run(cmd+list(map(str,sources))+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(c.stdout+c.stderr);assert not c.returncode,c.stderr
 command=['xcrun','simctl','spawn',a.simulator_device,str(exe),str(fixture)] if a.simulator_device else [str(exe),str(fixture)];run=subprocess.run(command,capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
faults={}
for name in ['overlap','oversize','unterminated']:
 exe=out/'normal';command=['xcrun','simctl','spawn',a.simulator_device,str(exe),str(fixture),name] if a.simulator_device else [str(exe),str(fixture),name];run=subprocess.run(command,capture_output=True,text=True,timeout=10);(out/(name+'-fault.log')).write_text(run.stdout+run.stderr);assert run.returncode==(134 if a.simulator_device else -6) and 'UTF-8 conversion unsupported' in run.stderr,(name,run.returncode,run.stderr);faults[name]={'returncode':run.returncode,'state':'explicit unsupported abort'}
paths=sources+[R/'port/core/guest_encoding.h',R/'port/core/cp1252_table.inc',R/'port/core/nls_classification.inc',R/'port/core/nls_casing.inc',R/'config/encoding-sources.json',fixture,case_header];report={'state':'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'runs':runs,'faults':faults,'cases':len(rows),'unicode_scalars':scalar_count,'oracle':'CPython UTF-8/UTF-16 strict and replacement codecs; all Unicode scalar values, fixed malformed boundaries and seeded random strings','sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'test_driver_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'scope':'Actual ARM64 conversion/metadata wrappers, declared modern Unicode profile; no Windows DLL parity, native QoL startup or game-session claim'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
