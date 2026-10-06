"""Apply source-bound finite zlib call routes and exact original writer gates."""
import bisect,hashlib,json,pathlib,re
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 root=R/'generated/integration-review/library-routes';meta=json.loads((root/'result.json').read_text());test=json.loads((root/'test-result.json').read_text());trace=json.loads((R/meta['trace_path']).read_text());assert sha(R/meta['trace_path'])==meta['trace_sha256'] and sha(root/'module.ll')==meta['module_sha256'];assert meta['script_sha256']==sha(R/'scripts/prepare-library-routes.py') and test['passed'] and test['variant_sha256']==sha(root/'result.json') and test['script_sha256']==sha(R/'scripts/test-library-routes.py')
 routed=(root/'module.ll').read_text();source=engine.read_text()
 def body(text,name):
  needle='define hidden fastcc void @'+name+'(';assert text.count(needle)==1;start=text.index(needle);end=text.index('\n}',start)+2;return start,end,text[start:end]
 for row in meta['routes']:
  start,end,old=body(source,row['name']);assert hashlib.sha256(old.encode()).hexdigest()==row['body_sha256'];_,_,new=body(routed,row['name']);assert hashlib.sha256(new.encode()).hexdigest()==row['routed_sha256'];source=source[:start]+new+source[end:]
  _,_,router=body(routed,row['router']);source+='\n'+router+'\n'
 for target in sorted({r['new_target'] for r in meta['routes']}):
  if not re.search(r'^(?:declare|define).*@loc_%X\('%target,source,re.M):source+='declare hidden fastcc void @loc_%X(%%_cpu*)\n'%target
 source+='declare i32 @qol_library_route_ready()\n';engine.write_text(source)
 entries=sorted((int(m[2],16),m.start(),m[1]) for m in re.finditer(r'^define hidden fastcc void @(loc_([0-9A-F]+))\(',dll,re.M));addresses=[e[0] for e in entries];groups={};records=[]
 original=pefile.PE(str(R/'ref/product/IFC22.dll'));decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
 for row in trace['code_writes']:
  groups.setdefault(entries[bisect.bisect_right(addresses,row['pc'])-1],[]).append(row);records.append('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(row['pc'],row['address'],row['size'],int.from_bytes(bytes.fromhex(row['before']),'little'),int.from_bytes(bytes.fromhex(row['after']),'little')))
 replacements=[];transforms=[]
 for (_,start,name),rows in groups.items():
  end=dll.index('\n}',start)+2;old=dll[start:end];lines=old.splitlines(keepends=True);pointers=set(re.findall(r'(%[0-9]+) = inttoptr ',old));candidates=[]
  for i,line in enumerate(lines):
   m=re.fullmatch(r'store i32 ([^,]+), i32\* (%[0-9]+), align [0-9]+\n',line)
   if m and m[2] in pointers:candidates.append((i,m))
  assert len(rows)==len(candidates)
  for (index,m),row in zip(candidates,rows):
   ins=next(decoder.disasm(original.get_data(row['pc']-0x10000000,15),row['pc']));assert ins.mnemonic=='mov' and ins.operands[0].type==capstone.x86.X86_OP_MEM and ins.operands[0].size==4
   prefix='tb_library_%d_'%index;lines[index]='%%%sp = bitcast i32* %s to i8*\n%%%sv = zext i32 %s to i64\ncall void @qol_library_code_write(%%_cpu* %%cpu, i32 %d, i8* %%%sp, i32 4, i64 %%%sv)\n'%(prefix,m[2],prefix,m[1],row['pc'],prefix,prefix)
  new=''.join(lines);replacements.append((start,end,new));transforms.append(dict(function=name,before_sha256=hashlib.sha256(old.encode()).hexdigest(),after_sha256=hashlib.sha256(new.encode()).hexdigest()))
 for start,end,new in sorted(replacements,reverse=True):dll=dll[:start]+new+dll[end:]
 dll+='\ndeclare void @qol_library_code_write(%_cpu*, i32, i8*, i32, i64)\ndeclare void @qol_library_checkpoint_return(%_cpu*)\n';needle='define hidden fastcc void @loc_10061027(%_cpu* %cpu) nounwind {\n';assert dll.count(needle)==1;dll=dll.replace(needle,needle+'call void @qol_library_checkpoint_return(%_cpu* %cpu)\n')
 include=out/'library_publication.inc';include.write_text('static const TribesCodeWrite library_code_writes[]={'+','.join(records)+'};\nstatic const uint32_t library_data_addresses[]={'+','.join(str(d['address'])+'u' for d in meta['data'])+'};\nstatic const unsigned char library_data_after[]={'+','.join(str(b) for b in bytes.fromhex(meta['data'][0]['after']))+'};\n')
 result=dict(routes_sha256=sha(root/'result.json'),tests_sha256=sha(root/'test-result.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),trace_sha256=meta['trace_sha256'],transforms=transforms,extra_image_pages=sorted({d['address']&~4095 for d in meta['data']}),engine_sha256=sha(engine),include_sha256=sha(include),scope='Exact native writer gates and compiled call routers; activation only after all25 original stores, three separate verified data flushes. No generic code-write allowance.')
 (out/'library-patch-inputs.json').write_text(json.dumps(result,indent=2)+'\n');return dll,result
