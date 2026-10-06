#!/usr/bin/env python3
"""Actual ARM64 GetLocaleInfoEx against pinned primary-source golden expectations."""
import argparse,hashlib,importlib.util,json,pathlib,re,struct,subprocess
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();out=R/'generated/native-win32/locale-info-abi'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"state":"running","pass":false}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({'GetLocaleInfoEx':bindings['GetLocaleInfoEx']}));subprocess.run(['python3',str(R/'scripts/gen-nls-info.py'),'--check'],check=True,capture_output=True)
header=R/'ref/downloads/wine-10.0-winnls.h';tests=R/'ref/downloads/wine-10.0-locale-tests.c';config=json.loads((R/'config/encoding-sources.json').read_text());refs=config['collation_reference']['supporting_sources']
for path in (header,tests):assert hashlib.sha256(path.read_bytes()).hexdigest()==next(v['sha256'] for v in refs.values() if v['local_path']==str(path.relative_to(R)))
constants={k:int(v,16) for k,v in re.findall(r'^#define\s+(LOCALE_\w+)\s+(0[xX][0-9a-fA-F]+)',header.read_text(),re.M)};cases=[]
def add(locale,kind,want):cases.append((locale,kind,(want+'\0').encode('utf-16-le') if isinstance(want,str) else want))
# Independent invariant_list from Wine's primary regression tests. Native
# implementation output is never used to generate expected results.
source=tests.read_text();block=source[source.index('static const struct invariant_entry invariant_list[]'):source.index('#undef X',source.index('static const struct invariant_entry invariant_list[]'))];reference_cases=0
for name,want in re.findall(r'\{ X\((LOCALE_\w+)\),\s*"([^"\n]*)"',block):
 want=re.sub(r'\\x([a-fA-F0-9]+)',lambda m:chr(int(m[1],16)),want)
 # The primary test explicitly permits empty invariant country abbreviation.
 if name=='LOCALE_SABBREVCTRYNAME':want=''
 add('',constants[name],want);reference_cases+=1
 if name.startswith('LOCALE_I'):add('',constants[name]|0x20000000,struct.pack('<I',int(want,16 if name in ('LOCALE_ILANGUAGE','LOCALE_IDEFAULTLANGUAGE') else 10)))
for loc,kind,want in [('en',0x5c,'en'),('en-US',0x5c,'en-US'),('eN-uS',0x5c,'en-US'),('en',7,'USA'),('en',3,'ENU'),('en',0x6d,''),('en-US',0x6d,'en'),('en',6,'United States'),('en-US',0xe,'.'),('fr-FR',0xe,','),('en-IN',0x10,'3;2;0'),('fr-FR',0x14,'€'),('ru-RU',0x3c,'май'),('ru-RU',0x1000003c,'мая'),('ru-RU',0x2a,'понедельник'),('ja-JP',0x2a,'月曜日'),('th-TH',0x1009,'7'),('en-US',0x1000003c,'May'),('en-US',0x100e,''),('de-DE_phoneb',0x5c,'de-DE_phoneb'),(None,0x5c,'en-US'),('!x-sys-default-locale',0x5c,'en-US')]:add(loc,kind,want)
for loc,kind,num in [('en',1,0x409),('en',0x71,1),('en-US',0x71,0),('th-TH',0x1009,7),('en-US',0x1004,1252),('en-US',0x100c,6),('en-US',0x7b,0)]:
 if kind!=0x7b:add(loc,kind|0x20000000,struct.pack('<I',num))
# Binary font signature expectations come directly from the pinned data record,
# independently sliced using the documented field offset, not native output.
raw=(R/'ref/downloads/wine-10.0-locale.nls').read_bytes();u16=lambda p:struct.unpack_from('<H',raw,p)[0];u32=lambda p:struct.unpack_from('<I',raw,p)[0];base=u32(16);strings=base+u32(base+0x40);index=base+u32(base+0x30)
for i in range(u16(base+0x28)):
 pos,record,_=struct.unpack_from('<HHI',raw,index+8*i);p=strings+2*pos;name=raw[p+2:p+2+u16(p)*2].decode('utf-16-le')
 if name=='en-US':
  record_at=base+u32(base+0x24)+record*u16(base+0x22);font=strings+2*u32(record_at+0xac);assert u16(font)==16;add(name,0x58,raw[font+2:font+34]);break
else:raise AssertionError('Missing en-US')
# Validate immutable no-user-override and CP_ACP flags on every expectation.
base_cases=list(cases)
for loc,kind,want in base_cases:
 for flag in (0x80000000,0x40000000):add(loc,kind|flag,want)
text='/* Golden invariant regression values from pinned Wine tests; additional explicit API-contract and Unicode cases. */\n';rows=[]
for i,(loc,kind,want) in enumerate(cases):
 name='NULL'
 if loc is not None:
  name=f'loc_{i}';rawloc=(loc+'\0').encode('utf-16-le');text+='static const uint16_t '+name+'[]={'+','.join(str(int.from_bytes(rawloc[j:j+2],'little')) for j in range(0,len(rawloc),2))+'};\n'
 expected=f'want_{i}';text+='static const unsigned char '+expected+'[]={'+','.join(map(str,want))+'};\n';rows.append('{%s,0x%xu,%s,%d}'%(name,kind,expected,len(want)))
text+='static const struct InfoCase info_cases[]={\n'+',\n'.join(rows)+'\n};\n';fixture=out/'locale_info_cases.h';fixture.write_text(text)
sources=[R/'port/core/guest_locale_info.c',R/'port/core/guest_heap.c',R/'tests/locale_info_abi_test.c',R/'tests/fixtures/native_enter.ll',wrapper];runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 c=subprocess.run(cmd+list(map(str,sources))+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(c.stdout+c.stderr);assert not c.returncode,c.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
exe=out/'normal';run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe),'sort-name'] if a.simulator_device else [str(exe),'sort-name'],capture_output=True,text=True,timeout=10);(out/'sort-name-fault.log').write_text(run.stdout+run.stderr);assert run.returncode==(134 if a.simulator_device else -6) and 'locale information unsupported' in run.stderr
paths=sources+[R/'port/core/guest_locale_info.h',R/'port/core/nls_info.inc',R/'scripts/gen-nls-info.py',R/'config/encoding-sources.json',fixture,header,tests];report={'state':'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'runs':runs,'faults':{'sort-name':{'returncode':run.returncode,'state':'explicit unsupported abort'}},'golden_cases':len(cases),'pinned_invariant_text_expectations':reference_cases,'sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'test_driver_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'scope':'Actual ARM64 service wrappers; pinned immutable Wine locale data, no registry overrides. No selected QoL native startup/game session or actual Windows DLL execution claim'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
