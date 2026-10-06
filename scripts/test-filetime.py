#!/usr/bin/env python3
"""Exercise the reached UTC SYSTEMTIME/FILETIME pair through its emitted guest ABI."""
import datetime,zoneinfo,hashlib,importlib.util,json,os,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/filetime';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
epoch=datetime.datetime(1601,1,1)
dates=[(1601,1,65535,1,0,0,0,0),(1604,2,123,29,23,59,59,999),(1700,3,6,1,12,34,56,789),(1900,3,0,1,0,0,0,0),(1969,12,0,31,23,59,59,999),(1970,1,5,1,0,0,0,0),(2000,2,3,29,1,2,3,4),(2026,10,65535,6,17,42,46,593),(2038,1,0,19,3,14,8,0),(2400,2,7,29,0,0,0,0),(9999,12,0,31,23,59,59,999),(30827,12,0,31,23,59,59,999)]
def forward(v):
 # Python datetime is independent of the native adapter and supports years
 # through 9999. Gregorian 400-year cycles extend the upper FILETIME cases.
 cycles,year=divmod(v[0]-1601,400);year+=1601
 delta=datetime.datetime(year,v[1],v[3],v[4],v[5],v[6],v[7]*1000)-epoch
 return ((delta.days+cycles*146097)*86400+delta.seconds)*10000000+delta.microseconds*10
def inverse(ticks):
 days,clock=divmod(ticks//10000,86400000);cycles,days=divmod(days,146097)
 date=epoch+datetime.timedelta(days=days,milliseconds=clock)
 return [date.year+cycles*400,date.month,(date.weekday()+1)%7,date.day,date.hour,date.minute,date.second,date.microsecond//1000]
ticks=[forward(v) for v in dates]+list(range(0,10000,997))+[0x7fffffffffffffff]
golden=out/'filetime-golden.h';golden.write_text('static const uint16_t dates[][8]={'+','.join('{'+','.join(map(str,v))+'}' for v in dates)+'};\nstatic const uint64_t forward_golden[]={'+','.join('UINT64_C(%d)'%forward(v) for v in dates)+'};\nstatic const struct {uint64_t ticks;uint16_t fields[8];} inverse_golden[]={'+','.join('{UINT64_C(%d),{%s}}'%(t,','.join(map(str,inverse(t)))) for t in ticks)+'};\n')
(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({n:bindings[n] for n in ['SystemTimeToFileTime','FileTimeToSystemTime','FileTimeToLocalFileTime','LocalFileTimeToFileTime']}))
lookup=out/'lookup.ll';lookup.write_text('''declare hidden fastcc void @tbimp_SystemTimeToFileTime(ptr)
declare hidden fastcc void @tbimp_FileTimeToSystemTime(ptr)
declare hidden fastcc void @tbimp_FileTimeToLocalFileTime(ptr)
declare hidden fastcc void @tbimp_LocalFileTimeToFileTime(ptr)
declare fastcc void @tribes_return_to_host(ptr)
declare void @abort() noreturn
define ptr @tribes_lookup(i32 %pc) {
 switch i32 %pc, label %bad [i32 1, label %local i32 2, label %inverse i32 3, label %to_local i32 4, label %to_utc i32 4294963200, label %host]
local: ret ptr @tbimp_SystemTimeToFileTime
inverse: ret ptr @tbimp_FileTimeToSystemTime
to_local: ret ptr @tbimp_FileTimeToLocalFileTime
to_utc: ret ptr @tbimp_LocalFileTimeToFileTime
host: ret ptr @tribes_return_to_host
bad: call void @abort()
 unreachable
}
''')
enter=R/'tests/fixtures/native_enter.ll'
support=[R/'port/core/guest_datetime.c',R/'port/core/guest_heap.c',wrapper,lookup,enter]
objects,_=compile_support(support,out/'checked',[]);meta=json.loads((out/'checked/result.json').read_text())
entries=[dict(object=str(p),object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']) for p in map(pathlib.Path,objects)]
guarded,_=guard_link(entries,out/'guarded')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
fixture=R/'tests/filetime_test.c';barrier=R/'port/core/guest_memory_checks.c';external=R/'port/core/guest_external_checks.c'
flags=['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-I',str(out),'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module'];runs=[]
for mode,inputs in [('normal',support),('checked',guarded)]:
 exe=out/(mode+'-test');subprocess.run(flags+list(map(str,[fixture,barrier,external,*inputs]))+['-o',str(exe)],check=True)
 for timezone in ['UTC','America/Chicago','Asia/Kathmandu']:
  offset=int(datetime.datetime.now(zoneinfo.ZoneInfo(timezone)).utcoffset().total_seconds())
  run=subprocess.run([str(exe),str(offset)],env={**os.environ,'TZ':timezone},capture_output=True,text=True,timeout=20);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(mode,timezone,run.stdout,run.stderr)
  runs.append(dict(mode=mode,timezone=timezone,current_offset_seconds=offset,output=run.stdout.strip(),binary_sha256=sha(exe)))
 print('PASS:',mode,'UTC conversion independent of UTC, Chicago and Kathmandu host timezones',flush=True)
faults=[]
for name in ['input-short','output-short','input-protected','output-readonly','inverse-input-short','inverse-output-short']+[prefix+'-'+case for prefix in ['to-local','to-utc'] for case in ['input-short','output-short','input-protected','output-readonly']]:
 run=subprocess.run([str(out/'checked-test'),name],capture_output=True,text=True,timeout=20);(out/(name+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==-6 and 'memory violation' in run.stderr,(name,run.returncode,run.stderr)
 faults.append(dict(name=name,returncode=run.returncode))
paths=[pathlib.Path(__file__).resolve(),fixture,R/'port/core/guest_datetime.h',*support[:2],enter,barrier,external,R/'port/llasm-runtime/win32-bindings.json']
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',runs=runs,checked_faults=faults,golden_sha256=sha(golden),sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Native and checked emitted UTC and local FILETIME conversion pairs, independent Python datetime/cycle golden fields and zoneinfo current-bias oracle, input/output/overlap boundaries, invalid-date failure and protected-memory checks. Component evidence; not original-game or playable-map acceptance.'),indent=2)+'\n')
