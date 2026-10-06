"""Bind the verified 114-write string/utility phase, without publishing its new bank records."""
import bisect,hashlib,json,pathlib,re
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 root=R/'generated/integration-review/string-patched-engine';vm=json.loads((root/'variants/result.json').read_text());test=json.loads((root/'variants/test-result.json').read_text());parent=json.loads((root/'result.json').read_text());tp=R/parent['trace_path'];trace=json.loads(tp.read_text())
 assert vm['preparation_sha256']==sha(root/'result.json') and vm['trace_sha256']==sha(tp)==parent['trace_sha256']
 assert not vm['removed'] and test['passed'] and not test['unvisited_targets'] and test['variants_sha256']==sha(root/'variants/result.json') and test['script_sha256']==sha(R/'scripts/test-string-patch-variants.py')
 assert sha(root/'variants/module.ll')==vm['module_sha256'] and parent['script_sha256']==sha(R/'scripts/prepare-string-patches.py') and vm['script_sha256']==sha(R/'scripts/prepare-string-patch-variants.py')
 frozen=R/'generated/integration-review/http-publication-checkpoint-20261003'
 assert trace['script_sha256']==sha(frozen/'sources/scripts/trace-string-qol-patches.py') and trace['completed_patch_routine'] and trace['stop']['pc']==0x10061036
 assert all(sha(R/n)==h for n,h in test['support_sha256'].items())
 order_path=R/'generated/integration-review/string-patch-order/order.json';order=json.loads(order_path.read_text())
 assert order['passed'] and order['trace_sha256']==sha(tp) and order['script_sha256']==sha(R/'scripts/record-string-patch-order.py')

 original=engine.read_text();variant=(root/'variants/module.ll').read_text();selected=sorted(vm['function_sha256']);added=set(vm['added']);wrappers=['declare i32 @qol_string_route_ready()','declare void @qol_string_unavailable_entry() noreturn']
 # One pass over the large engine preserves exact bodies and avoids rescanning
 # hundreds of megabytes once per wrapper.
 renamed=set()
 def rename_existing(match):
  name=match[1]
  if name not in selected:return match[0]
  assert name not in added and name not in renamed;renamed.add(name)
  return 'define hidden fastcc void @tribes_before_string_'+name+'('
 original=re.sub(r'^define (?:hidden|protected) fastcc void @(loc_[0-9A-F]+)\(',rename_existing,original,flags=re.M)
 assert renamed==set(selected)-added
 for name in selected:
  pattern=r'^define (?:hidden|protected) fastcc void @'+name+r'\('
  variant,count=re.subn(pattern,'define hidden fastcc void @tribes_string_'+name+'(',variant,flags=re.M);assert count==1,name
  before=(' call void @qol_string_unavailable_entry()\n unreachable\n' if name in added else ' musttail call fastcc void @tribes_before_string_%s(%%_cpu* %%cpu)\n ret void\n'%name)
  wrappers.append('''declare hidden fastcc void @tribes_string_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_string_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_string_%s(%%_cpu* %%cpu)
 ret void
before:
%s}
'''%(name,name,name,before))
 declared=set(re.findall(r'^declare .*@([A-Za-z_0-9]+)\(',variant,re.M));variant+='\n'+''.join('declare hidden fastcc void @%s(%%_cpu*)\n'%name for name in selected if name not in declared)
 engine.write_text(original+'\n'+'\n'.join(wrappers));variant_path=out/'string-engine.ll';variant_path.write_text(variant)
 rows=[r for r in trace['code_writes'] if r['image']=='engine'];assert len(rows)==114
 entries=sorted((int(m[2],16),m.start(),m[1]) for m in re.finditer(r'^define hidden fastcc void @(loc_([0-9A-F]+))\(',dll,re.M));addresses=[e[0] for e in entries];groups={}
 for row in rows:groups.setdefault(entries[bisect.bisect_right(addresses,row['pc'])-1],{}).setdefault(row['pc'],row)
 pe=pefile.PE(str(R/'ref/product/IFC22.dll'));decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True;replacements=[];transforms=[]
 shared_pcs={0x100fbcdd,0x100fbcf3,0x100fbd10,0x100fbd12}
 for (_,start,name),by_pc in groups.items():
  writes=list(by_pc.values());end=dll.index('\n}',start)+3;body=dll[start:end];lines=body.splitlines(keepends=True)
  pointers=set(re.findall(r'(%[0-9]+) = inttoptr ',body));candidates=[]
  for i,line in enumerate(lines):
   # Original scalar MOV stores have alignment1; retain alignment4 stack
   # argument/return stores unchanged.
   store=re.fullmatch(r'store i(8|16|32|64) ([^,]+), i\1\* (%[0-9]+), align 1\n',line)
   helper=re.fullmatch(r'call void @x86_mov_mem_reg_(8|16|32)\(i32 ([0-9]+), i32 (%[0-9]+|[0-9]+)\) nounwind\n',line)
   if store and store[3] in pointers:candidates.append((i,'store',store))
   if helper:candidates.append((i,'helper',helper))
   vector=re.fullmatch(r'call void @tb_sse_store\(%_cpu\* %cpu, i32 ([0-9]+), i32 ([^,]+), i32 ([0-9]+)\) nounwind\n',line)
   if vector:candidates.append((i,'vector',vector))
  assert len(candidates)==len(writes),(name,len(candidates),len(writes))
  for index,((li,kind,m),row) in enumerate(zip(candidates,writes)):
   ins=next(decoder.disasm(pe.get_data(row['pc']-0x10000000,15),row['pc']));prefix='tb_string_%d_'%index
   if kind=='vector':
    assert ins.mnemonic in {'movups','movq'} and ins.operands[0].type==capstone.x86.X86_OP_MEM
    assert int(m[1])==0 and (ins.operands[0].size,int(m[3])) in {(16,4),(8,2)}
    assert ins.operands[1].reg==capstone.x86.X86_REG_XMM0
    assert row['pc'] in {0x10012272,0x10012c53,0x10012c5e}
    lines[li]='call void @qol_string_vector_write(%%_cpu* %%cpu, i32 %d, i32 %s, i32 %s, i32 %s)\n'%(row['pc'],m[2],m[1],m[3])
    transforms.append(dict(function=name,pc=row['pc'],instruction=ins.mnemonic+' '+ins.op_str,original_sha256=hashlib.sha256(body.encode()).hexdigest()));continue
   assert ins.mnemonic=='mov' and ins.operands[0].type==capstone.x86.X86_OP_MEM and ins.operands[0].size==row['size']
   width=int(m[1]);assert width==row['size']*8
   if kind=='store':
    replacement='%%%sp = bitcast i%d* %s to i8*\n'%(prefix,width,m[3])
    if width<64:replacement+='%%%sv = zext i%d %s to i64\n'%(prefix,width,m[2]);value='%'+prefix+'v'
    else:value=m[2]
   else:
    address,value=int(m[2]),m[3];assert address==row['address']
    replacement='%%%sbasep = getelementptr %%_cpu, %%_cpu* %%cpu, i32 0, i32 13\n%%%sbase = load i64, i64* %%%sbasep, align 8\n%%%sa = add i64 %%%sbase, %d\n%%%sp = inttoptr i64 %%%sa to i8*\n'%(prefix,prefix,prefix,prefix,prefix,address,prefix,prefix)
    if value.startswith('%'):
     if width<32:replacement+='%%%snarrow = trunc i32 %s to i%d\n'%(prefix,value,width);value='%'+prefix+'narrow'
     replacement+='%%%sv = zext i%d %s to i64\n'%(prefix,width,value);value='%'+prefix+'v'
    else:
     value=str(int(value)&((1<<width)-1));assert int(value)==int.from_bytes(bytes.fromhex(row['after']),'little')
   helper_name='qol_string_shared_write' if row['pc'] in shared_pcs else 'qol_string_code_write'
   replacement+='call void @%s(%%_cpu* %%cpu, i32 %d, i8* %%%sp, i32 %d, i64 %s)\n'%(helper_name,row['pc'],prefix,row['size'],value)
   lines[li]=replacement;transforms.append(dict(function=name,pc=row['pc'],instruction=ins.mnemonic+' '+ins.op_str,original_sha256=hashlib.sha256(body.encode()).hexdigest()))
  replacements.append((start,end,''.join(lines)))
 for start,end,body in sorted(replacements,reverse=True):dll=dll[:start]+body+dll[end:]
 dll+='\ndeclare void @qol_string_shared_write(%_cpu*, i32, i8*, i32, i64)\n'
 dll+='\ndeclare void @qol_string_code_write(%_cpu*, i32, i8*, i32, i64)\ndeclare void @qol_string_vector_write(%_cpu*, i32, i32, i32, i32)\n'
 dll+='declare void @qol_string_checkpoint_return(%_cpu*)\n';needle='define hidden fastcc void @loc_10061036(%_cpu* %cpu) nounwind {\n';assert dll.count(needle)==1;dll=dll.replace(needle,needle+'call void @qol_string_checkpoint_return(%_cpu* %cpu)\n')
 records=['{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(r['pc'],r['address'],r['size'],int.from_bytes(bytes.fromhex(r['before']),'little'),int.from_bytes(bytes.fromhex(r['after']),'little')) for r in rows]
 flushes=order['flushes'];assert len(flushes)==12 and flushes[-1]['engine_writes']==114
 arrays=['static const TribesCodeWrite string_code_writes[]={'+','.join(records)+'};']
 for i,f in enumerate(flushes):
  data=bytes.fromhex(f['bytes_hex']);assert len(data)==f['size']
  arrays.append('static const unsigned char string_flush_%d[]={%s};'%(i,','.join(map(str,data))))
 arrays.append('static const struct {uint32_t address,size,writes;const unsigned char *bytes;} string_flushes[]={'+','.join('{%du,%du,%du,string_flush_%d}'%(f['address'],f['size'],f['engine_writes'],i) for i,f in enumerate(flushes))+'};')
 include=out/'string_publication.inc';include.write_text('\n'.join(arrays)+'\n')
 extra=sorted({page for s in trace['services'] if s['name']=='VirtualProtect' for page in s['effect']['pages'] if not 0x3c0000<=page<0x3d0000})
 metadata=dict(trace_sha256=sha(tp),variants_sha256=sha(root/'variants/result.json'),tests_sha256=sha(root/'variants/test-result.json'),functions=selected,added=sorted(added),transforms=transforms,extra_image_pages=extra,include_sha256=sha(include),variant_sha256=sha(variant_path),engine_sha256=sha(engine),script_sha256=sha(pathlib.Path(__file__).resolve()),flush_order_sha256=sha(order_path),scope='Observed114 ordered engine writes; activate after its final intermediate flush. Shared memset sites retain checked ordinary writes outside the exact pending plan. The three newly registered bank records remain unpublished. The reference suffix reaches the enclosing routine return; no native equivalence claim from preparation.')
 (out/'string-patch-inputs.json').write_text(json.dumps(metadata,indent=2)+'\n');return dll,variant_path,metadata
