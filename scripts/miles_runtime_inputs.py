"""Bind original Miles imports to its finite translated image, never success stubs."""
import hashlib,json,pathlib,shutil
R=pathlib.Path(__file__).resolve().parents[1]
SOURCE=R/'generated/srw/run-20261004T204722-mss32/native-diagnostic'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(out,imports):
 m=json.loads((SOURCE/'result.json').read_text());assert m['prepared'] and m['diagnostic_only']
 profile=json.loads((R/'config/runtime/miles-mixer.json').read_text())['flat_segments'];assert (profile['ds'],profile['es'],profile['push_bytes'],profile['base'],profile['limit'])==(0x23,0x23,4,0,0xffffffff)
 assert sha(R/'ref/product/Mss32.dll')==m['image_sha256'] and sha(SOURCE/'module.ll')==m['module_sha256'] and sha(SOURCE/'image.bin')==m['mapped_sha256']
 for p,h in m['source_sha256'].items():assert sha(R/p)==h,p
 linked={}
 for row in imports:
  if row['dll'].casefold()!='mss32.dll':continue
  target=m['exports'][row['name']];assert [target,'loc_%X'%target] in m['entries']
  item=dict(symbol=row['symbol'],name=row['name'],target=target)
  assert row['symbol'] not in linked or linked[row['symbol']]==item
  linked[row['symbol']]=item
 lines=['declare hidden fastcc void @loc_%X(ptr)'%v for v in sorted({r['target'] for r in linked.values()})]
 lines+=['define hidden fastcc void @%s(ptr %%cpu){musttail call fastcc void @loc_%X(ptr %%cpu)\nret void\n}'%(r['symbol'],r['target']) for r in linked.values()]
 wrapper=out/'miles-imports.ll';wrapper.write_text('\n'.join(lines)+'\n')
 shutil.copyfile(SOURCE/'image.bin',out/'miles.bin')
 (out/'miles_bootstrap.inc').write_text('#define TB_MILES_IMAGE_BASE 0x%xu\n#define TB_MILES_IMAGE_SIZE %du\n#define TB_MILES_ENTRY 0x%xu\n'%(m['image_base'],m['image_size'],m['entry']))
 return [SOURCE/'module.ll',wrapper,R/'port/core/miles_flat_segments.c'],m,list(linked.values())
