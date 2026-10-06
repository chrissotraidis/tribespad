#!/usr/bin/env python3
"""Bind captured direct-call rewrites to finite compiled target routers.
Unchanged instructions and return-address pushes retain their original LLVM.
"""
import bisect,hashlib,json,pathlib,re,struct
import pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/library-routes';out.mkdir(exist_ok=True)
trace_path=R/'generated/integration-review/library-patch-trace/result.json';trace=json.loads(trace_path.read_text());assert trace['completed_patch_routine'] and not trace['unconfirmed_writes'] and sha(R/'scripts/trace-library-qol-patches.py')==trace['script_sha256']
checkpoint=R/trace['checkpoint_path'];assert sha(checkpoint)==trace['checkpoint_sha256'];capture=json.loads(checkpoint.read_text());pages=json.loads((R/capture['library_patch_checkpoint']['library-patch-pages.json']['path']).read_text())['ranges'];memory={}
for row in pages:
 meta=capture['library_patch_checkpoint'][row['file']];p=R/meta['path'];assert sha(p)==meta['sha256'];memory[row['address']]=p.read_bytes()
def read(address,size):
 row=next(r for r in pages if r['address']<=address and address+size<=r['address']+r['size']);return memory[row['address']][address-row['address']:address-row['address']+size]
compiler_image=R/'generated/integration-review/patched-engine/Tribes2.exe';compiler_pe=pefile.PE(str(compiler_image))
engine=R/'generated/integration-review/patched-engine/va/engine.ll';source=engine.read_text();entries=sorted((int(m[1],16),m.start()) for m in re.finditer(r'^define hidden fastcc void @loc_([0-9A-F]+)\(',source,re.M));addresses=[a for a,_ in entries];late=set(json.loads((R/'generated/integration-review/late-patched-engine/variants/result.json').read_text())['changed_existing']);dll=(R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/module.ll').read_text();original=pefile.PE(str(R/'ref/product/IFC22.dll'));routes=[];wrappers=[];functions=[]
for w in trace['code_writes']:
 assert w['completed'] and w['image']=='engine' and w['size']==4 and read(w['address'],4).hex()==w['before']
 site=w['address']-1;assert read(site,1)==b'\xe8';old=(site+5+int.from_bytes(bytes.fromhex(w['before']),'little'))&0xffffffff;new=(site+5+int.from_bytes(bytes.fromhex(w['after']),'little'))&0xffffffff
 assert w['value']&0xffffffff==int.from_bytes(bytes.fromhex(w['after']),'little')
 entry,start=entries[bisect.bisect_right(addresses,site)-1];name='loc_%X'%entry;assert name not in late;end=source.index('\n}',start)+2;body=source[start:end]
 assert read(entry,site+5-entry)==compiler_pe.get_data(entry-0x400000,site+5-entry)
 old_call='musttail call fastcc void @loc_%X(%%_cpu* %%cpu) nounwind'%old;assert body.count(old_call)==1 and 'store i32 %d,'% (site+5) in body
 assert re.findall(r'musttail call fastcc void @(\w+)',body)==['loc_%X'%old]
 assert ('define hidden fastcc void @loc_%X('%new) in dll
 router='tribes_library_call_%X'%site;replacement=body.replace(old_call,'musttail call fastcc void @%s(%%_cpu* %%cpu) nounwind'%router)
 wrapper='''define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%phase = call i32 @qol_library_route_ready()
 %%ready = icmp ne i32 %%phase, 0
 br i1 %%ready, label %%patched, label %%original
patched:
 musttail call fastcc void @loc_%X(%%_cpu* %%cpu)
 ret void
original:
 musttail call fastcc void @loc_%X(%%_cpu* %%cpu)
 ret void
}
'''%(router,new,old)
 routes.append(dict(entry=entry,name=name,site=site,old_target=old,new_target=new,writer=w,body_sha256=hashlib.sha256(body.encode()).hexdigest(),routed_sha256=hashlib.sha256(replacement.encode()).hexdigest(),router=router));functions.append(replacement);wrappers.append(wrapper)
assert len(routes)==25 and len({r['name'] for r in routes})==25
expected=b'1.3.1.zlib-ng\0';assert len(expected)==14 and original.get_data(0x1ba200,14)==expected
data=[]
for address in [0x757220,0x783168,0x784c0c]:
 writes=[w for w in trace['image_data_writes'] if address<=w['address']<address+14];assert [w['size'] for w in writes]==[8,4,2] and b''.join(bytes.fromhex(w['after']) for w in writes)==expected
 data.append(dict(address=address,before=read(address,14).hex(),after=expected.hex(),size=14))
assert len(trace['image_data_writes'])==9
engine_pe=pefile.PE(str(R/'ref/control/Tribes2.exe'))
for d in data:
 section=next(s for s in engine_pe.sections if s.VirtualAddress<=d['address']-0x400000<s.VirtualAddress+s.Misc_VirtualSize);assert not section.Characteristics&0x20000000
# Every changed engine byte must be accounted for, including version data.
working={a:bytearray(b) for a,b in memory.items()}
for w in trace['code_writes']+trace['image_data_writes']:
 row=next(r for r in pages if r['address']<=w['address'] and w['address']+w['size']<=r['address']+r['size']);off=w['address']-row['address'];assert working[row['address']][off:off+w['size']].hex()==w['before'];working[row['address']][off:off+w['size']]=bytes.fromhex(w['after'])
for row in trace['changed_regions']:
 if 0x400000<=row['address']<0x1000000:
  p=R/row['after_path'];assert sha(p)==row['after_sha256'] and bytes(working[row['address']])==p.read_bytes()
header=re.search(r'^%_cpu = type .*$',source,re.M)[0];targets=sorted({r[k] for r in routes for k in ['old_target','new_target']});module=out/'module.ll';module.write_text(header+'\ndeclare i32 @qol_library_route_ready()\n'+''.join('declare hidden fastcc void @loc_%X(%%_cpu*)\n'%t for t in targets)+'\n'.join(wrappers+functions))
report=dict(state='compiled_routes_prepared_not_published',trace_path=str(trace_path.relative_to(R)),trace_sha256=sha(trace_path),checkpoint_path=str(checkpoint.relative_to(R)),checkpoint_sha256=sha(checkpoint),engine_sha256=sha(engine),compiler_image_sha256=sha(compiler_image),dll_sha256=hashlib.sha256(dll.encode()).hexdigest(),routes=routes,data=data,module_sha256=sha(module),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Exactly25 direct-call target selections; original complete LLVM blocks retain every instruction and return-address push, changing only the final transfer to a finite state router. All replacement targets exist in the compiled DLL. Three non-code version strings are separate. Runtime writes/activation and differential tests pending.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Prepared25 finite call routers and3 source-verified version updates')
