#!/usr/bin/env python3
"""Execute actual emitted ARM64 date/time wrappers against explicit golden cases."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
sdk_name,profile_target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();out=R/'generated/native-win32/datetime-abi'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"state":"running","pass":false}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({k:bindings[k] for k in ('GetDateFormatEx','GetTimeFormatEx')}));subprocess.run(['python3',str(R/'scripts/gen-nls-datetime.py'),'--check'],check=True,capture_output=True)
# Wine 10.0 kernel32 locale regression expectations, with additional Gregorian,
# Unicode-locale and Microsoft picture contracts. These are expected results,
# not outputs copied from the implementation being tested.
date=(2002,5,45612,4,65432,34512,65535,12345);time=(0,0,0,0,8,56,13,22);cases=[]
def add(target,flags,fmt,want,t=None,locale='en-US'):cases.append((target,flags,locale,fmt,t or (date if target==1 else time),want))
for fmt,want in [("ddd',' MMM dd yy",'Sat, May 04 02'),("ddd',' MMM dd ''''yy","Sat, May 04 '02"),('dddd d MMMM yyyy','Saturday 4 May 2002'),('M/MM/d/dd','5/05/4/04'),("'abc''bar' yyyy","abc'bar 2002"),('',''),('y/yy/yyyy/yyyyy','2/02/2002/2002')]:add(1,0,fmt,want)
for flags,want in [(0,'5/4/2002'),(1,'5/4/2002'),(2,'Saturday, May 4, 2002'),(8,'May 2002'),(128,'May 4')]:add(1,0x80000000|flags,None,want)
add(1,0,'dddd d MMMM yyyy','Wednesday 23 October 2002',(2002,10,45612,23,65432,34512,65535,12345));add(1,0,'dddd d MMMM yyyy','Monday 1 January 1601',(1601,1,0,1,0,0,0,0));add(1,0,'yyyy-MM-dd','2000-02-29',(2000,2,99,29,0,0,0,0));add(1,0,'yyyy','30827',(30827,1,0,1,0,0,0,0));add(1,0,'dddd d MMMM yyyy','samedi 4 mai 2002',locale='fr-FR');add(1,0,'MMMM','май',locale='ru-RU');add(1,0,'ddMMMM','04мая',locale='ru-RU');add(1,0,'MMMMddd dd','майсб 04',locale='ru-RU');add(1,0,'MMMMdd','мая04',locale='ru-RU')
for flags,fmt,want in [(8,"tt HH':'mm'@'ss",'AM 08:56@13'),(1,'m1s2m3s4',''),(2,'h:m:s tt','8:56 AM'),(2,'h.@:m.@:s.@:tt','8.@:56AM'),(2,'s1s2s3',''),(0,'t/tt','A/AM'),(0,'HH:mm:ss','08:56:13'),(0,"'abc''bar' hh", "abc'bar 08"),(0,'','')]:add(2,flags,fmt,want)
for flags,want in [(0,'8:56:13 AM'),(1,'8 AM'),(2,'8:56 AM'),(4,'8:56:13'),(8,'8:56:13 AM')]:add(2,0x80000000|flags,None,want)
add(2,0,'t/tt','P/PM',(0,0,0,0,13,56,13,0));add(2,4,'h1t2tt3m','156',(0,0,0,0,13,56,13,0));add(2,8,'h:m:s tt','13:56:13 PM',(0,0,0,0,13,56,13,0));add(2,0,'h:mm:ss tt','12:00:00 AM',(0,0,0,0,0,0,0,0));add(2,0,'h:mm:ss tt','12:00:00 PM',(0,0,0,0,12,0,0,0));add(1,0,'z'*600,'z'*600);add(2,0,'z'*600,'z'*600)
text='/* Independent explicit datetime regression expectations; see test driver and pinned Wine source. */\n';rows=[]
for i,(target,flags,loc,fmt,t,want) in enumerate(cases):
 names=[]
 for j,s in enumerate((loc,fmt,want)):
  if s is None:names.append('NULL');continue
  name=f's_{i}_{j}';names.append(name);raw=(s+'\0').encode('utf-16-le');values=[int.from_bytes(raw[k:k+2],'little') for k in range(0,len(raw),2)];text+=f'static const uint16_t {name}[]={{'+','.join(map(str,values))+'};\n'
 rows.append('{%du,0x%xu,%s,%s,{%s},%s}'%(target,flags,names[0],names[1],','.join(map(str,t)),names[2]))
text+='static const struct FormatCase format_cases[]={\n'+',\n'.join(rows)+'\n};\n';fixture=out/'datetime_cases.h';fixture.write_text(text)
sources=[R/'port/core/guest_datetime.c',R/'port/core/guest_heap.c',R/'tests/datetime_abi_test.c',R/'tests/fixtures/native_enter.ll',wrapper];runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',profile_target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 c=subprocess.run(cmd+list(map(str,sources))+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(c.stdout+c.stderr);assert not c.returncode,c.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
faults={}
for name in ['calendar','layout','long-format']:
 exe=out/'normal';run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe),name] if a.simulator_device else [str(exe),name],capture_output=True,text=True,timeout=10);(out/(name+'-fault.log')).write_text(run.stdout+run.stderr);assert run.returncode==(134 if a.simulator_device else -6) and 'date/time formatting unsupported' in run.stderr,(name,run.returncode,run.stderr);faults[name]={'returncode':run.returncode,'state':'explicit unsupported abort'}
paths=sources+[R/'port/core/guest_datetime.h',R/'port/core/nls_datetime.inc',R/'scripts/gen-nls-datetime.py',R/'config/encoding-sources.json',fixture];report={'state':'finished','pass':True,'target':profile_target,'simulator_device':a.simulator_device,'runs':runs,'faults':faults,'golden_cases':len(cases),'sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'test_driver_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'scope':'Actual ARM64 service wrappers; pinned Gregorian/Wine presentation profile. No selected QoL native startup/game-session or actual Windows DLL execution claim'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
