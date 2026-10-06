"""Bind the two original input patch stores to one precompiled engine variant."""
import hashlib,json,pathlib,re
import pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 root=R/'generated/integration-review/startup-input-variant';meta=json.loads((root/'result.json').read_text());test=json.loads((root/'test-result.json').read_text());trace=json.loads((root/'startup-prefix.json').read_text())
 assert meta['state']=='strict_unpublished_variant' and sha(root/'variant.ll')==meta['llvm_sha256']
 assert test['passed'] and test['variant_sha256']==sha(root/'result.json') and test['llvm_sha256']==meta['llvm_sha256'] and test['script_sha256']==sha(R/'scripts/test-startup-input-variant.py')
 assert meta['script_sha256']==sha(R/'scripts/prepare-startup-input-variant.py') and meta['original_sha256']==sha(R/'ref/control/Tribes2.exe')
 assert trace['passed'] and trace['original_sha256']==sha(R/'ref/product/IFC22.dll') and trace['source_checkpoint_sha256']==sha(R/trace['source_checkpoint'])
 assert trace['input_provider_data']['bytes_hex']=='49464332322e646c6c00'
 pe=pefile.PE(str(R/'ref/product/IFC22.dll'))
 for ins in trace['instructions']:assert pe.get_data(ins['pc']-0x10000000,len(bytes.fromhex(ins['bytes_hex']))).hex()==ins['bytes_hex']
 rows=trace['writes'];assert [(w['pc'],w['address'],w['size'],w['value']) for w in rows]==[(0x10060ca8,0x556d00,4,0x90909090),(0x10060caa,0x556d04,2,0x9090)]
 needle='define hidden fastcc void @loc_10060C9E(';start=dll.index(needle);end=dll.index('\n}',start)+3;body=dll[start:end];lines=body.splitlines(keepends=True);pointers=set(re.findall(r'(%[0-9]+) = inttoptr ',body));candidates=[]
 for i,line in enumerate(lines):
  m=re.fullmatch(r'store i(16|32) ([^,]+), i\1\* (%[0-9]+), align [0-9]+\n',line)
  if m and m[3] in pointers:candidates.append((i,m))
 assert len(candidates)==2
 for (index,m),row in zip(candidates,rows):
  width=int(m[1]);assert width==row['size']*8;prefix='tb_input_%d_'%index
  lines[index]='%%%sp = bitcast i%d* %s to i8*\n%%%sv = zext i%d %s to i64\ncall void @qol_input_code_write(%%_cpu* %%cpu, i32 %d, i8* %%%sp, i32 %d, i64 %%%sv)\n'%(prefix,width,m[3],prefix,width,m[2],row['pc'],prefix,row['size'],prefix)
 replacement=''.join(lines);dll=dll[:start]+replacement+dll[end:]+'\ndeclare void @qol_input_code_write(%_cpu*, i32, i8*, i32, i64)\n'
 source=engine.read_text();needle='define hidden fastcc void @loc_556CFF(';assert source.count(needle)==1
 source=source.replace(needle,'define hidden fastcc void @tribes_before_input_556CFF(',1)
 variant=(root/'variant.ll').read_text();m=re.search(r'^define hidden fastcc void @patched_input_block\(.*?^}',variant,re.M|re.S);assert m
 variant=m[0].replace('@patched_input_block','@tribes_after_input_556CFF').replace('@unchanged_next','@loc_556D06')
 source+='\n'+variant+'''
declare i32 @qol_input_route_ready()
define hidden fastcc void @loc_556CFF(%_cpu* %cpu) {
 %ready = call i32 @qol_input_route_ready()
 %patched = icmp ne i32 %ready, 0
 br i1 %patched, label %after, label %before
after:
 musttail call fastcc void @tribes_after_input_556CFF(%_cpu* %cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_input_556CFF(%_cpu* %cpu)
 ret void
}
''';engine.write_text(source)
 records=['{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],int.from_bytes(bytes.fromhex(w['before']),'little'),w['value']) for w in rows]
 include=out/'startup_input_code.inc';include.write_text('static const TribesCodeWrite input_code_writes[]={'+','.join(records)+'};\n')
 report=dict(variant_sha256=sha(root/'result.json'),test_sha256=sha(root/'test-result.json'),trace_sha256=sha(root/'startup-prefix.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),writer_before_sha256=hashlib.sha256(body.encode()).hexdigest(),writer_after_sha256=hashlib.sha256(replacement.encode()).hexdigest(),engine_sha256=sha(engine),include_sha256=sha(include),activation='After the second exact original store, before the next original instruction; initial route retained until then. No generic guest code mutation.')
 (out/'startup-input-code.json').write_text(json.dumps(report,indent=2)+'\n');return dll,report
