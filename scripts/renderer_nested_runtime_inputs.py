"""Bind the pre-memset renderer suffix to its finite compiled revision."""
import hashlib,json,pathlib
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/renderer-nested'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def definition(text,name):
 begin=text.find('define hidden fastcc void @'+name+'(')
 if begin<0:begin=text.find('define protected fastcc void @'+name+'(')
 assert begin>=0,name
 return text[begin:text.index('\n}',begin)+3]
def prepare(dll,engine,out):
 m=json.loads((P/'preparation.json').read_text());t=json.loads((P/'test-result.json').read_text())
 assert m['passed'] and t['passed'] and t['cases']==4608 and t['preparation_sha256']==sha(P/'preparation.json') and t['script_sha256']==sha(R/'scripts/test-renderer-nested.py')
 for key,root in [('inputs_sha256',R),('outputs_sha256',P)]:
  for n,h in m[key].items():assert sha(root/n)==h,n
 text=engine.read_text();variant=(P/'engine.ll').read_text();wrappers=['declare i32 @qol_renderer_nested_route_ready(i32)']
 for row in m['records']:
  n=row['name'];body=definition(text,n);assert hashlib.sha256(body.encode()).hexdigest()==row['before_sha256'],n
  assert text.count(body)==1,n
  text=text.replace(body,body.replace('@'+n+'(','@tribes_before_renderer_nested_'+n+'(',1),1)
  variant=variant.replace('define hidden fastcc void @'+n+'(','define hidden fastcc void @tribes_renderer_nested_'+n+'(',1)
  wrappers.append(f'''declare hidden fastcc void @tribes_renderer_nested_{n}(%_cpu*)
define hidden fastcc void @{n}(%_cpu* %cpu) {{
 %ready = call i32 @qol_renderer_nested_route_ready(i32 {0 if row["entry"] in (0x6b4a71,0x6b3848,0x6afd2d) else 1})
 %active = icmp ne i32 %ready, 0
 br i1 %active, label %after, label %before
after:
 musttail call fastcc void @tribes_renderer_nested_{n}(%_cpu* %cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_renderer_nested_{n}(%_cpu* %cpu)
 ret void
}}
''')
 variant+='\n'+''.join('declare hidden fastcc void @'+r['name']+'(%_cpu*)\n' for r in m['records'])
 # The exact offcut is provided by the checked renderer continuation module.
 engine.write_text(text+'\n'+'\n'.join(wrappers));path=out/'renderer-nested-engine.ll';path.write_text(variant)
 originals={w['writer_name']:definition(dll,w['writer_name']) for w in m['writes']}
 for i,w in enumerate(m['writes']):
  name=w['writer_name'];assert hashlib.sha256(originals[name].encode()).hexdigest()==w['writer_sha256']
  body=definition(dll,name);needle=w['store'];assert body.count(needle)==1
  if w.get('absolute_helper'):
   replacement=f'%nested_base_ptr{i} = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13\n%nested_base{i} = load i64, i64* %nested_base_ptr{i}, align 8\n%nested_address{i} = add i64 %nested_base{i}, {w["address"]}\n%nested_pointer{i} = inttoptr i64 %nested_address{i} to i8*\ncall void @qol_renderer_nested_write(%_cpu* %cpu, i32 {w["pc"]}, i8* %nested_pointer{i}, i32 {w["size"]}, i64 {w["value"]})'
  else:
   replacement=f'%batch_pointer{i} = bitcast i{w["bits"]}* {w["pointer_operand"]} to i8*\n%batch_value{i} = zext i{w["bits"]} {w["value_operand"]} to i64\ncall void @qol_renderer_nested_write(%_cpu* %cpu, i32 {w["pc"]}, i8* %batch_pointer{i}, i32 {w["size"]}, i64 %batch_value{i})'
  assert dll.count(body)==1,name
  dll=dll.replace(body,body.replace(needle,replacement),1)
 dll+='\ndeclare void @qol_renderer_nested_write(%_cpu*, i32, i8*, i32, i64)\n'
 inc=out/'renderer_nested_publication.inc';inc.write_text('static const TribesCodeWrite renderer_nested_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in m['writes'])+'};\n')
 # Original table replacements request writable image-data pages. Validate the
 # exact author instructions and PE section before extending the loader policy.
 pe=pefile.PE(str(R/'ref/control/Tribes2.exe'));qol=pefile.PE(str(R/'ref/qol/IFC22.dll'))
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
 data_writes=[]
 for pc,address,value in [(0x1004d359,0x7c3a08,0x1004cef0),(0x1004d376,0x7c3a48,0x1004cef0),(0x1004d393,0x7c3a88,0x1004cef0)]:
  ins=next(md.disasm(qol.get_data(pc-0x10000000,16),pc));dst,src=ins.operands
  assert ins.mnemonic=='mov' and dst.type==capstone.x86.X86_OP_MEM and dst.size==4 and not dst.mem.base and not dst.mem.index and dst.mem.disp==address
  assert src.type==capstone.x86.X86_OP_IMM and src.imm==value
  section=pe.get_section_by_rva(address-0x400000);assert section and not(section.Characteristics&0x20000000)
  page=address&~4095;assert section.VirtualAddress+0x400000<=page and page+4096<=0x400000+section.VirtualAddress+section.Misc_VirtualSize
  data_writes.append(dict(pc=pc,address=address,value=value,page=page,instruction=bytes(ins.bytes).hex(),section=section.Name.rstrip(b'\0').decode()))
 report=dict(publication_groups=[5,17],exact_continuation_target=0x6b0c21,extra_image_pages=sorted({r['page'] for r in data_writes}),data_writes=data_writes,preparation_sha256=sha(P/'preparation.json'),test_sha256=sha(P/'test-result.json'),variant_sha256=sha(path),include_sha256=sha(inc),script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'renderer-nested-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,path,report
