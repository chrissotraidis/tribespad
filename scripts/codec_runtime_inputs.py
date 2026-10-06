"""Pinned original-address Miles ASI/M3D providers; actual DllMain runs only at guest LoadLibrary."""
import hashlib,json,pathlib,shutil,pefile
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def validate_catalog(rows):
 # Preserve the original ASI slots; new Miles plugins use a separate pool.
 # In particular fffa/fffb belong to dynamic graphics/system lookup, which
 # is separate from the merged static dispatch table's uniqueness check.
 slots=set();names=set()
 for row in rows:
  address=row['import_base'];name=row['module']
  assert address%0x10000==0 and (0xffe00000<=address<0xfff00000 or 0xfff20000<=address<0xfff60000),hex(address)
  assert address not in slots and name not in names,(name,hex(address))
  slots.add(address);names.add(name)
def prepare(out):
 sources=[];metadata=[];imports=[];definitions=[];thread_rows=[]
 catalog=json.loads((R/'config/runtime/miles-codecs.json').read_text());validate_catalog(catalog)
 for row in catalog:
  name=row['module'];source=R/row['lift']/'native-diagnostic';m=json.loads((source/'result.json').read_text())
  assert m['prepared'] and m['diagnostic_only'] and m['image_sha256']==row['sha256']==sha(R/row['product_path'])
  assert sha(source/'module.ll')==m['module_sha256'] and sha(source/'image.bin')==m['mapped_sha256']
  for p,h in m['source_sha256'].items():assert sha(R/p)==h,p
  assert not m['exports'];pe=pefile.PE(str(R/row['product_path']));assert not hasattr(pe,'DIRECTORY_ENTRY_TLS')
  sources.append(source/'module.ll');imports+=m['imports'];m['config']=row;metadata.append(m)
  shutil.copyfile(source/'image.bin',out/(name+'.bin'))
  path='C:\\TribesPad\\Game\\'+pathlib.Path(row['product_path']).name
  definitions.append('{0x%xu,%du,0x%xu,%s,%s}'%(m['image_base'],m['image_size'],m['entry'],json.dumps(path),json.dumps(name+'.bin')))
  thread_rows.append('{0x%xu,0x%xu,UINT32_MAX,0,0,0,NULL,NULL,1}'%(m['image_base'],m['entry']))
 (out/'codec_bootstrap.inc').write_text('static const struct {uint32_t image,size,entry;const char *path,*file;} tb_codecs[]={'+','.join(definitions)+'};\n#define TB_CODEC_THREAD_MODULES '+','.join(thread_rows)+'\n')
 return sources,metadata,imports
