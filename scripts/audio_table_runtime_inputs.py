"""Bind original audio operand writes to their strict finite ARM64 variants."""
import hashlib,json,pathlib,re
import capstone,pefile
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/audio-tables'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 m=json.loads((P/'preparation.json').read_text());t=json.loads((P/'test-result.json').read_text())
 assert m['passed'] and t['passed'] and t['preparation_sha256']==sha(P/'preparation.json') and t['script_sha256']==sha(R/'scripts/test-audio-tables.py')
 for key,root in [('inputs_sha256',R),('outputs_sha256',P)]:
  for n,h in m[key].items():assert sha(root/n)==h,n
 text=engine.read_text();variant=(P/'engine.ll').read_text();wrappers=['declare i32 @qol_audio_tables_route_ready()']
 for row in m['records']:
  n=row['name'];body=definition(text,n)
  assert hashlib.sha256(re.sub(r'^define protected ','define hidden ',body).encode()).hexdigest()==row['before_sha256'],n
  text=text.replace(body,body.replace('@'+n+'(','@tribes_before_audio_tables_'+n+'(',1),1)
  variant=re.sub(r'define (?:hidden|protected) fastcc void @'+n+r'\(',f'define hidden fastcc void @tribes_audio_tables_{n}(',variant,count=1)
  wrappers.append(f'''declare hidden fastcc void @tribes_audio_tables_{n}(%_cpu*)
define hidden fastcc void @{n}(%_cpu* %cpu) {{
 %ready = call i32 @qol_audio_tables_route_ready()
 %active = icmp ne i32 %ready, 0
 br i1 %active, label %after, label %before
after:
 musttail call fastcc void @tribes_audio_tables_{n}(%_cpu* %cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_audio_tables_{n}(%_cpu* %cpu)
 ret void
}}
''')
 engine.write_text(text+'\n'+'\n'.join(wrappers))
 # Calls between selected blocks must re-enter their gated original-address routes.
 for row in m['records']:
  n=row['name']
  if not re.search(r'^declare .*@'+n+r'\(',variant,re.M):variant+='\ndeclare hidden fastcc void @'+n+'(%_cpu*)\n'
 path=out/'audio-tables-engine.ll';path.write_text(variant)
 for pc in sorted({w['pc'] for w in m['writes']}):
  ws=[w for w in m['writes'] if w['pc']==pc];size=ws[0]['size'];value=ws[0]['value'];assert all(w['size']==size and w['value']==value for w in ws)
  name=f'loc_{pc:X}';body=definition(dll,name)
  operand=str(value)
  if size==1:
   trunc=re.search(r'^(%\d+) = trunc i32 '+str(value)+r' to i8$',body,re.M);assert trunc,name;operand=trunc[1]
  matches=list(re.finditer(r'^store i'+str(size*8)+' '+re.escape(operand)+r', i'+str(size*8)+r'\* (%\d+), align \d+$',body,re.M));assert len(matches)==1,(name,matches)
  match=matches[0];pointer=match[1]
  assert re.search(r'^'+re.escape(pointer)+r' = inttoptr ',body,re.M)
  replacement=f'%audio_pointer = bitcast i{size*8}* {pointer} to i8*\n%audio_value = zext i{size*8} {operand} to i64\ncall void @qol_audio_tables_write(%_cpu* %cpu, i32 {pc}, i8* %audio_pointer, i32 {size}, i64 %audio_value)'
  dll=dll.replace(body,body.replace(match[0],replacement),1)
 dll+='\ndeclare void @qol_audio_tables_write(%_cpu*, i32, i8*, i32, i64)\n'
 inc=out/'audio_tables_publication.inc';inc.write_text('static const TribesCodeWrite audio_table_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in m['writes'])+'};\n')
 # Admit only the original conditional data-table pages, never executable pages.
 pe=pefile.PE(str(R/'ref/control/Tribes2.exe'));q=pefile.PE(str(R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll'))
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
 data=[]
 for pc,address,value in [(0x10058b5c,0x7a3138,0x10057d20),(0x10055a74,0x74ac58,63)]:
  ins=next(md.disasm(q.get_data(pc-0x10000000,16),pc));dst,src=ins.operands
  assert ins.mnemonic=='mov' and dst.type==capstone.x86.X86_OP_MEM and dst.size==4 and not dst.mem.base and not dst.mem.index and dst.mem.disp==address and src.type==capstone.x86.X86_OP_IMM and src.imm==value
  section=pe.get_section_by_rva(address-0x400000);page=address&~4095
  assert section and not(section.Characteristics&0x20000000) and section.VirtualAddress+0x400000<=page and page+4096<=0x400000+section.VirtualAddress+section.Misc_VirtualSize
  data.append(dict(pc=pc,address=address,value=value,page=page,instruction=bytes(ins.bytes).hex(),section=section.Name.rstrip(b'\0').decode()))
 report=dict(extra_image_pages=sorted({r['page'] for r in data}),data_writes=data,preparation_sha256=sha(P/'preparation.json'),test_sha256=sha(P/'test-result.json'),variant_sha256=sha(path),include_sha256=sha(inc),script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'audio-tables-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,path,report
