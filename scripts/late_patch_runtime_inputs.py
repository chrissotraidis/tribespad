"""Bind one source-verified late write sequence and its exact AOT variants."""
import bisect,hashlib,json,pathlib,re
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare_late(dll,engine,out):
 plan_path=R/'generated/integration-review/late-patch-plan.json';plan=json.loads(plan_path.read_text());assert plan['verified'] and not plan['native_activated']
 trace_path=R/plan['trace_path'];assert sha(trace_path)==plan['trace_sha256'];trace=json.loads(trace_path.read_text());assert trace['completed_patch_routine']
 variants=R/'generated/integration-review/late-patched-engine/variants';vm=json.loads((variants/'result.json').read_text());test=json.loads((variants/'test-result.json').read_text())
 assert vm['state']=='compiled_unpublished' and vm['plan_sha256']==sha(plan_path) and not vm['added'] and not vm['removed']
 assert sha(variants/'module.ll')==vm['module_sha256'] and sha(engine)==vm['old_llvm_sha256']
 assert test['passed'] and test['variants_sha256']==sha(variants/'result.json') and test['plan_sha256']==sha(plan_path)
 assert sha(R/'scripts/test-late-engine-variants.py')==test['script_sha256']
 selected=vm['changed_existing'];original=engine.read_text();late=(variants/'module.ll').read_text();wrappers=['declare i32 @qol_late_route_state()']
 for name in selected:
  needle='define hidden fastcc void @'+name+'(';late_needle='define protected fastcc void @'+name+'(';assert original.count(needle)==late.count(late_needle)==1
  original=original.replace(needle,'define hidden fastcc void @tribes_initial_'+name+'(',1)
  late=late.replace(late_needle,'define hidden fastcc void @tribes_late_'+name+'(',1)
  wrappers.append('''declare hidden fastcc void @tribes_late_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%phase = call i32 @qol_late_route_state()
 %%ready = icmp eq i32 %%phase, 2
 br i1 %%ready, label %%late, label %%initial
late:
 musttail call fastcc void @tribes_late_%s(%%_cpu* %%cpu)
 ret void
initial:
 musttail call fastcc void @tribes_initial_%s(%%_cpu* %%cpu)
 ret void
}
'''%(name,name,name,name))
 # References between variants retain their public original-address wrappers.
 late+='\n'+''.join('declare hidden fastcc void @%s(%%_cpu*)\n'%n for n in selected)
 engine_path=out/'engine-routed.ll';engine_path.write_text(original+'\n'+'\n'.join(wrappers));late_path=out/'late-engine.ll';late_path.write_text(late)
 entries=sorted((int(m[2],16),m.start(),m[1]) for m in re.finditer(r'^define hidden fastcc void @(loc_([0-9A-F]+))\(',dll,re.M));addresses=[e[0] for e in entries];groups={}
 for row in plan['code_writes']:
  entry=entries[bisect.bisect_right(addresses,row['pc'])-1];groups.setdefault(entry,[]).append(row)
 pe=pefile.PE(str(R/'ref/product/IFC22.dll'));decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
 replacements=[];transforms=[]
 for (address,start,name),writes in groups.items():
  end=dll.index('\n}',start)+3;body=dll[start:end];lines=body.splitlines(keepends=True)
  pointers=set(re.findall(r'(%[0-9]+) = inttoptr ',body));candidates=[]
  for i,line in enumerate(lines):
   store=re.fullmatch(r'store i(8|16|32|64) ([^,]+), i\1\* (%[0-9]+), align [0-9]+\n',line)
   helper=re.fullmatch(r'call void @x86_mov_mem_reg_8\(i32 ([0-9]+), i32 ([0-9]+)\) nounwind\n',line)
   if store and store[3] in pointers:candidates.append((i,'store',store))
   if helper:candidates.append((i,'helper',helper))
  assert len(candidates)==len(writes),(name,len(candidates),len(writes))
  for index,((line_index,kind,match),row) in enumerate(zip(candidates,writes)):
   ins=next(decoder.disasm(pe.get_data(row['pc']-pe.OPTIONAL_HEADER.ImageBase,15),row['pc']))
   assert ins.mnemonic=='mov' and ins.operands[0].type==capstone.x86.X86_OP_MEM and ins.operands[0].size==row['size']
   prefix='tb_late_%d_'%index
   if kind=='store':
    width=int(match[1]);assert width==row['size']*8
    new='%%%spointer = bitcast i%d* %s to i8*\n'%(prefix,width,match[3])
    if width<64:new+='%%%svalue = zext i%d %s to i64\n'%(prefix,width,match[2]);value='%'+prefix+'value'
    else:value=match[2]
   else:
    guest,value_int=int(match[1]),int(match[2]);assert row['size']==1 and guest==row['address'] and value_int==row['value']<256
    new='%%%sbasep = getelementptr %%_cpu, %%_cpu* %%cpu, i32 0, i32 13\n'%prefix
    new+='%%%sbase = load i64, i64* %%%sbasep, align 8\n'%(prefix,prefix)
    new+='%%%saddress = add i64 %%%sbase, %d\n'%(prefix,prefix,guest)
    new+='%%%spointer = inttoptr i64 %%%saddress to i8*\n'%(prefix,prefix);value=str(value_int)
   new+='call void @qol_late_code_write(%%_cpu* %%cpu, i32 %d, i8* %%%spointer, i32 %d, i64 %s)\n'%(row['pc'],prefix,row['size'],value)
   transforms.append(dict(function=name,writer_pc=row['pc'],original_instruction=lines[line_index].strip(),replacement=new.strip(),function_before_sha256=hashlib.sha256(body.encode()).hexdigest()))
   lines[line_index]=new
  replacements.append((start,end,''.join(lines)))
 for start,end,replacement in sorted(replacements,reverse=True):dll=dll[:start]+replacement+dll[end:]
 dll+='\ndeclare void @qol_late_code_write(%_cpu*, i32, i8*, i32, i64)\ndeclare void @qol_late_checkpoint_return(%_cpu*)\n'
 needle='define hidden fastcc void @loc_1006174D(%_cpu* %cpu) nounwind {\n';assert dll.count(needle)==1
 dll=dll.replace(needle,needle+'call void @qol_late_checkpoint_return(%_cpu* %cpu)\n',1)
 records=[]
 for row in plan['code_writes']:
  stored=row['value']&((1<<(8*row['size']))-1);assert stored.to_bytes(row['size'],'little').hex()==row['after']
  records.append('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(row['pc'],row['address'],row['size'],int.from_bytes(bytes.fromhex(row['before']),'little'),stored))
 (out/'late_publication.inc').write_text('static const TribesCodeWrite late_code_writes[]={'+','.join(records)+'};\n')
 extra_pages=sorted({page for e in trace['services'] if e['name']=='VirtualProtect' and e['arguments'][2] in (0x20,0x40) for page in range(e['arguments'][0]&~4095,(e['arguments'][0]+e['arguments'][1]+4095)&~4095,4096) if page!=0x3c0000 and not 0x3c0000<=page<0x3d0000})
 metadata=dict(plan_sha256=sha(plan_path),trace_sha256=sha(trace_path),variants_sha256=sha(variants/'result.json'),tests_sha256=sha(variants/'test-result.json'),functions=selected,transforms=transforms,extra_image_pages=extra_pages,source_sha256=sha(pathlib.Path(__file__).resolve()),outputs_sha256={str(p.relative_to(R)):sha(p) for p in [engine_path,late_path,out/'late_publication.inc']})
 (out/'late-patch-inputs.json').write_text(json.dumps(metadata,indent=2)+'\n')
 return dll,engine_path,late_path,metadata
