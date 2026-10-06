"""Bind the verified 104-write prefix, without publishing its new bank records."""
import bisect,hashlib,json,pathlib,re
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 root=R/'generated/integration-review/followup-prefix-engine';vm=json.loads((root/'variants/result.json').read_text());test=json.loads((root/'variants/test-result.json').read_text());parent=json.loads((root/'result.json').read_text());tp=R/parent['trace_path'];trace=json.loads(tp.read_text())
 assert vm['preparation_sha256']==sha(root/'result.json') and vm['trace_sha256']==sha(tp)==parent['trace_sha256']
 assert not vm['removed'] and test['passed'] and not test['unvisited_targets'] and test['variants_sha256']==sha(root/'variants/result.json') and test['script_sha256']==sha(R/'scripts/test-followup-prefix-variants.py')
 assert sha(root/'variants/module.ll')==vm['module_sha256'] and parent['script_sha256']==sha(R/'scripts/prepare-followup-prefix.py') and vm['script_sha256']==sha(R/'scripts/prepare-followup-prefix-variants.py')
 assert trace['script_sha256']==sha(R/'scripts/trace-followup-qol-patches.py') and not trace['completed_patch_routine'] and trace['stop']['name']=='GetAdaptersAddresses'
 original=engine.read_text();variant=(root/'variants/module.ll').read_text();selected=sorted(vm['function_sha256']);added=set(vm['added']);wrappers=['declare i32 @qol_followup_route_ready()','declare void @qol_followup_unavailable_entry() noreturn']
 # One pass over the large engine preserves exact bodies and avoids rescanning
 # hundreds of megabytes once per wrapper.
 renamed=set()
 def rename_existing(match):
  name=match[1]
  if name not in selected:return match[0]
  assert name not in added and name not in renamed;renamed.add(name)
  return 'define hidden fastcc void @tribes_before_followup_'+name+'('
 original=re.sub(r'^define (?:hidden|protected) fastcc void @(loc_[0-9A-F]+)\(',rename_existing,original,flags=re.M)
 assert renamed==set(selected)-added
 for name in selected:
  pattern=r'^define (?:hidden|protected) fastcc void @'+name+r'\('
  variant,count=re.subn(pattern,'define hidden fastcc void @tribes_followup_'+name+'(',variant,flags=re.M);assert count==1,name
  before=(' call void @qol_followup_unavailable_entry()\n unreachable\n' if name in added else ' musttail call fastcc void @tribes_before_followup_%s(%%_cpu* %%cpu)\n ret void\n'%name)
  wrappers.append('''declare hidden fastcc void @tribes_followup_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_followup_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_followup_%s(%%_cpu* %%cpu)
 ret void
before:
%s}
'''%(name,name,name,before))
 declared=set(re.findall(r'^declare .*@([A-Za-z_0-9]+)\(',variant,re.M));variant+='\n'+''.join('declare hidden fastcc void @%s(%%_cpu*)\n'%name for name in selected if name not in declared)
 engine.write_text(original+'\n'+'\n'.join(wrappers));variant_path=out/'followup-engine.ll';variant_path.write_text(variant)
 rows=[r for r in trace['code_writes'] if r['image']=='engine'];assert len(rows)==104
 entries=sorted((int(m[2],16),m.start(),m[1]) for m in re.finditer(r'^define hidden fastcc void @(loc_([0-9A-F]+))\(',dll,re.M));addresses=[e[0] for e in entries];groups={}
 for row in rows:groups.setdefault(entries[bisect.bisect_right(addresses,row['pc'])-1],{})[row['pc']]=row
 pe=pefile.PE(str(R/'ref/product/IFC22.dll'));decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True;replacements=[];transforms=[]
 for (_,start,name),by_pc in groups.items():
  writes=list(by_pc.values());end=dll.index('\n}',start)+3;body=dll[start:end];lines=body.splitlines(keepends=True);pointers=set(re.findall(r'(%[0-9]+) = inttoptr ',body));candidates=[]
  for i,line in enumerate(lines):
   # Original MOV operands in these exact blocks have alignment1. The three
   # alignment4 stores in loc_10028116 are PUSH arguments/return, retained.
   store=re.fullmatch(r'store i(8|16|32|64) ([^,]+), i\1\* (%[0-9]+), align 1\n',line)
   helper=re.fullmatch(r'call void @x86_mov_mem_reg_(8|32)\(i32 ([0-9]+), i32 ([0-9]+)\) nounwind\n',line)
   if store and store[3] in pointers:candidates.append((i,'store',store))
   if helper:candidates.append((i,'helper',helper))
  assert len(candidates)==len(writes),(name,len(candidates),len(writes))
  for index,((li,kind,m),row) in enumerate(zip(candidates,writes)):
   ins=next(decoder.disasm(pe.get_data(row['pc']-0x10000000,15),row['pc']));assert ins.mnemonic=='mov' and ins.operands[0].type==capstone.x86.X86_OP_MEM and ins.operands[0].size==row['size'];prefix='tb_followup_%d_'%index
   width=int(m[1]);assert width==row['size']*8
   if kind=='store':
    replacement='%%%sp = bitcast i%d* %s to i8*\n'%(prefix,width,m[3])
    if width<64:replacement+='%%%sv = zext i%d %s to i64\n'%(prefix,width,m[2]);value='%'+prefix+'v'
    else:value=m[2]
   else:
    address,value=int(m[2]),m[3];assert address==row['address'] and int(value)==int.from_bytes(bytes.fromhex(row['after']),'little')
    replacement='%%%sbasep = getelementptr %%_cpu, %%_cpu* %%cpu, i32 0, i32 13\n%%%sbase = load i64, i64* %%%sbasep, align 8\n%%%sa = add i64 %%%sbase, %d\n%%%sp = inttoptr i64 %%%sa to i8*\n'%(prefix,prefix,prefix,prefix,prefix,address,prefix,prefix)
   replacement+='call void @qol_followup_code_write(%%_cpu* %%cpu, i32 %d, i8* %%%sp, i32 %d, i64 %s)\n'%(row['pc'],prefix,row['size'],value)
   lines[li]=replacement;transforms.append(dict(function=name,pc=row['pc'],instruction=ins.mnemonic+' '+ins.op_str,original_sha256=hashlib.sha256(body.encode()).hexdigest()))
  replacements.append((start,end,''.join(lines)))
 for start,end,body in sorted(replacements,reverse=True):dll=dll[:start]+body+dll[end:]
 dll+='\ndeclare void @qol_followup_code_write(%_cpu*, i32, i8*, i32, i64)\n'
 records=['{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(r['pc'],r['address'],r['size'],int.from_bytes(bytes.fromhex(r['before']),'little'),int.from_bytes(bytes.fromhex(r['after']),'little')) for r in rows]
 flushes=[s for s in trace['services'] if s['name']=='FlushInstructionCache'];assert [(s['effect']['address'],s['effect']['bytes']) for s in flushes]==[(0x798ca8,43),(0x5c0cfe,6)]
 arrays=['static const TribesCodeWrite followup_code_writes[]={'+','.join(records)+'};']
 for i,s in enumerate(flushes):
  address,size=s['effect']['address'],s['effect']['bytes'];region=next(r for r in trace['changed_regions'] if r['address']<=address and address+size<=r['address']+r['size']);path=R/region['after_path'];assert sha(path)==region['after_sha256'];data=path.read_bytes()[address-region['address']:address-region['address']+size]
  arrays.append('static const unsigned char followup_flush_%d[]={%s};'%(i,','.join(map(str,data))))
 include=out/'followup_publication.inc';include.write_text('\n'.join(arrays)+'\n')
 extra=sorted({page for s in trace['services'] if s['name']=='VirtualProtect' for page in s['effect']['pages'] if not 0x3c0000<=page<0x3d0000})
 metadata=dict(trace_sha256=sha(tp),variants_sha256=sha(root/'variants/result.json'),tests_sha256=sha(root/'variants/test-result.json'),functions=selected,added=sorted(added),transforms=transforms,extra_image_pages=extra,include_sha256=sha(include),variant_sha256=sha(variant_path),engine_sha256=sha(engine),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Observed104 ordered engine writes only; activate compiled prefix after final write. Six bank records stay unpublished; entire enclosing routine is not complete.')
 (out/'followup-patch-inputs.json').write_text(json.dumps(metadata,indent=2)+'\n');return dll,variant_path,metadata
