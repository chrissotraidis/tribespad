#!/usr/bin/env python3
"""Prepare IFC22 with original guest VAs and explicit diagnostic traps.

Separate from the production engine preparer: this artifact can only establish
native diagnostic boundaries. It must never be linked as a playable client.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import re
import subprocess
import sys
import time
import pefile

R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py')
va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
p=argparse.ArgumentParser(description=__doc__);p.add_argument('lift',type=pathlib.Path);p.add_argument('--compile',action='store_true');a=p.parse_args()
work=a.lift.resolve();identity=json.loads((work/'result.json').read_text())
assert identity['exit']==0 and identity['full_flags'] and identity['diagnostic']
image=work/'IFC22.dll';assert sha(image)==identity['image_sha256']==sha(R/'ref/product/IFC22.dll')
assert identity['native_zlib_routes'] is not None
for name,h in identity['native_zlib_routes']['source_sha256'].items():assert sha(R/name)==h,name
pe=pefile.PE(str(image));assert pe.OPTIONAL_HEADER.ImageBase==0x10000000
out=work/'native-diagnostic';out.mkdir(exist_ok=True)
report_path=out/'result.json'
if report_path.exists():
    old=json.loads(report_path.read_text())
    if old.get('state')=='compiling':
        live=subprocess.run(['ps','-p',str(old['pid']),'-o','command='],capture_output=True,text=True)
        assert not(live.returncode==0 and str(out/'module.ll') in live.stdout),'Compiler is still live'
report=dict(state='preparing',diagnostic_only=True,playable=False,native_module_entered=False,image_sha256=sha(image),srw_sha256=identity['srw_sha256'])
report_path.write_text(json.dumps(report,indent=2)+'\n')
def failed(kind,error,tb):
    report.update(state='failed',error_type=kind.__name__,error=str(error))
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    sys.__excepthook__(kind,error,tb)
sys.excepthook=failed
main=(work/'ifc22.llasm').read_text().splitlines();extern=(work/'extern.llinc').read_text()
known=set(re.findall(r'^proc (\S+) external','\n'.join(main)+'\n'+extern,re.M))
imports=[]
for dll in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in dll.imports:
        name=imp.name.decode() if imp.name else dll.dll.decode().split('.')[0]+'_ord'+str(imp.ordinal)
        assert name in known,(dll.dll,name)
        candidates=['ws2_32.'+name,dll.dll.decode().split('.')[0]+'_ord'+str(imp.ordinal)] if imp.import_by_ordinal else []
        sr_name=next((candidate for candidate in candidates if candidate in known),name)
        imports.append(dict(slot=imp.address,name=name,sr_name=sr_name,dll=dll.dll.decode().lower(),symbol=va.symbol(name),ordinal=imp.ordinal))
imports.sort(key=lambda x:x['slot']);assert len(imports)*16<0x10000
# Disjoint from the existing engine's 0xfffe0000 import window.
values={}
for i,row in enumerate(imports):
    row['guest_va']=0xfff00000+i*16
    assert row['name'] not in values,'Duplicate import name needs qualified binding'
    values[row['name']]=row['guest_va'];values[row['sr_name']]=row['guest_va']
exports={e.name.decode():0x10000000+e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name and not e.forwarder}
export_names={name:'loc_%X'%address for name,address in exports.items()}
aliases=sorted({'define '+name+' '+row['symbol'] for row in imports for name in [row['name'],row['sr_name']]})
declarations=['proc '+row['symbol']+' external' for row in imports]
extern='\n'.join(line for line in extern.splitlines() if not(re.fullmatch(r'proc (\S+) external',line) and line.split()[1] in values))
(out/'extern.llinc').write_text('\n'.join(aliases+declarations)+'\n'+extern+'\nproc tribes_dispatch external\nfuncv tribes_set_dispatch_target $, target\n')
(out/'macros.llinc').write_text('')
code=(work/'seg01_code.llinc').read_text()
# Preserve original export VAs instead of emitting MSVC-named host entry points.
for name,target in export_names.items():
    code=code.replace('proc '+name+' global c_'+name+'\n','proc '+target+'\n')
code=va.replace_names(code,export_names)
# SRW replacement spans that end exactly at the next procedure can omit endp.
# Close only an exact pinned tail-route body with a verified adjacent boundary.
# Other malformed procedures still fail in LLASM.
route_rows=json.loads((R/'port/llasm-runtime/qol-zlib-routes.json').read_text())['routes']
closures=[]
for route in route_rows:
    if 'replacement_length' not in route:continue
    pc=route['guest_va'];end=pc+route['replacement_length']
    old='proc loc_%X\ntcall tbimp_%s\nproc loc_%X\n'%(pc,route['binding'],end)
    if old in code:
        assert code.count(old)==1
        code=code.replace(old,old.replace('\nproc loc_%X\n'%end,'\nendp\n\nproc loc_%X\n'%end))
        closures.append(dict(pc=pc,next_pc=end,binding=route['binding']))
report['native_tail_route_closures']=closures
padding_removed=[]
for route in route_rows:
    if 'replacement_length' not in route:continue
    pc=route['guest_va'];end=pc+route['replacement_length']
    pattern=r'^proc loc_%X\n(tcall tbimp_%s\n)(.*?)^endp$'%(pc,re.escape(route['binding']))
    match=re.search(pattern,code,re.M|re.S);assert match,hex(pc)
    tail=match[2]
    if not tail:continue
    trailing=re.fullmatch(r'(?:(?:;int3\nINT 3\n)|\n)*tcall loc_([0-9A-F]+)\n',tail)
    assert trailing,('Unexpected code after pinned tail route',hex(pc),tail[:100])
    target=int(trailing[1],16);assert target>=end
    padding=pe.get_data(end-0x10000000,target-end)
    assert padding==b'\xcc'*(target-end) and tail.count('INT 3')==target-end
    code=code[:match.start(2)]+code[match.end(2):]
    padding_removed.append(dict(pc=pc,padding_begin=end,padding_end=target,bytes_hex=padding.hex()))
report['unreachable_tail_padding']=padding_removed


assert not re.search(r'^proc \?',code,re.M)
# Region partitioning retains literal tables. Four dormant fall-through edges
# after noreturn calls still name their data labels; retain explicit stops here.
# These are diagnostic-only boundaries, never synthesized playable code.
defined=set(re.findall(r'^proc (loc_[0-9A-F]+)',code,re.M))
references=set(re.findall(r'^\s*(?:tcall|ctcallz|ctcallnz)\s+.*?\b(loc_[0-9A-F]+)\s*$',code,re.M))
data_files=sorted(work.glob('seg*_data.llinc'))
data_labels=set().union(*(set(re.findall(r'^dlabel (loc_[0-9A-F]+)$',f.read_text(),re.M)) for f in data_files))
missing=sorted(references-defined)
assert set(missing)<=data_labels,('Unclassified missing direct target',set(missing)-data_labels)
for name in missing:code+='\nproc '+name+'\ncall halopad_trap_unimplemented '+hex(int(name[4:],16))+'\nendp\n'
report['data_target_traps']=missing

code,stats=va.transform(code.splitlines(),values)
(out/'code.llinc').write_text('\n'.join(code)+'\n')
kept=[];skip=False
for line in main:
    if re.fullmatch(r'proc (\S+) external',line) and line.split()[1] in values:continue
    if line.startswith('datasegment '):skip=True;continue
    if skip:
        if line.startswith('endd'):skip=False
        continue
    if line.startswith('define '):
        lhs,rhs=line.split()[1:];assert lhs in export_names or lhs.startswith(('loc_','c_'))
        continue
    kept.append('include code.llinc' if line=='include seg01_code.llinc' else line)
(out/'module.llasm').write_text('\n'.join(kept)+'\n')
tool=R/'generated/tools/llasm/llasm';assert sha(tool)==json.loads((R/'toolchains.lock.json').read_text())['artifacts']['llasm/llasm']
cmd=[str(tool),'-m64','-ptrofs','-O2','-I',str(out),'-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(out/'module.llasm')]
with (out/'llasm.log').open('w') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=log,check=True)
procs,removed=va.postprocess(out/'raw.ll',out/'module.ll')
entries=sorted([(int(n[4:],16),n) for n in procs]+[(x['guest_va'],x['symbol']) for x in imports])
assert len(entries)==len({x[0] for x in entries})
code_exports={name:address for name,address in exports.items() if pe.get_section_by_rva(address-0x10000000).Characteristics & 0x20000000}
data_exports={name:address for name,address in exports.items() if name not in code_exports}
assert all(address in {x[0] for x in entries} for address in code_exports.values())
assert not(set(data_exports.values()) & {x[0] for x in entries})
report['data_exports']=data_exports
(out/'dispatch.ll').write_text(va.dispatch_ir(entries))
t=pe.DIRECTORY_ENTRY_TLS.struct;callbacks=[]
for i in range(64):
    address=int.from_bytes(pe.get_data(t.AddressOfCallBacks-0x10000000+4*i,4),'little')
    if not address:break
    callbacks.append(address)
else:raise AssertionError('Unterminated TLS callbacks')
entry=0x10000000+pe.OPTIONAL_HEADER.AddressOfEntryPoint
assert {entry,*callbacks}<={x[0] for x in entries}
report.update(state='prepared',imports=imports,exports=exports,procedures=len(procs),stats=stats,wrappers_removed=removed,entry=entry,tls=dict(index_address=t.AddressOfIndex,template_hex=pe.get_data(t.StartAddressOfRawData-0x10000000,t.EndAddressOfRawData-t.StartAddressOfRawData).hex(),zero_fill=t.SizeOfZeroFill,callbacks=callbacks),module_ll_sha256=sha(out/'module.ll'),dispatch_sha256=sha(out/'dispatch.ll'),source_sha256={str(f.relative_to(R)):sha(f) for f in [pathlib.Path(__file__).resolve(),R/'scripts/prepare-va-model.py',work/'result.json',work/'seg01_code.llinc',work/'ifc22.llasm',work/'extern.llinc']},support_sha256={str(f.relative_to(R)):sha(f) for directory in [R/'port/llasm-support',R/'ref/halopad/port/llasm-support'] for f in directory.glob('*.llinc')})
report_path.write_text(json.dumps(report,indent=2)+'\n')
if a.compile:
    start=time.monotonic()
    with (out/'clang.log').open('w') as log:
        worker=subprocess.Popen(['clang','-target','arm64-apple-macos12','-O0','-Wno-override-module','-c',str(out/'module.ll'),'-o',str(out/'module.o')],stdout=log,stderr=log)
        report.update(state='compiling',pid=worker.pid);report_path.write_text(json.dumps(report,indent=2)+'\n')
        while True:
            try:exit=worker.wait(timeout=30);break
            except subprocess.TimeoutExpired:print('Same diagnostic ARM64 compiler running: PID',worker.pid,flush=True)
    report.update(state='compiled' if exit==0 else 'compile_failed',exit=exit,seconds=time.monotonic()-start)
    if exit==0:report.update(object_sha256=sha(out/'module.o'),object_bytes=(out/'module.o').stat().st_size)
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    assert exit==0,'See '+str(out/'clang.log')
print(json.dumps({k:v for k,v in report.items() if k not in ['imports','exports','source_sha256','support_sha256']},indent=2))
