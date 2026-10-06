#!/usr/bin/env python3
import hashlib,json,pathlib,pefile
from register_import_path import verify,START,PREFIX,RETURNS,WINDOW_START,WINDOW_PREFIX
R=pathlib.Path(__file__).resolve().parents[1];image=R/'ref/product/IFC22.dll';data=image.read_bytes();pe=pefile.PE(data=data)
row=next(r for r in json.loads((R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/result.json').read_text())['imports'] if r['name']=='curl_multi_setopt');regs={'esi':row['guest_va']};checks=0
for pc,length in RETURNS.items():
 assert verify(pe,data,row,pc,regs);checks+=1
 for wrong in [{},{'esi':0},{'esi':row['guest_va']+16}]:assert verify(pe,data,row,pc,wrong) is None;checks+=1
 for key,value in [('dll','kernel32.dll'),('name','curl_multi_init'),('slot',row['slot']+4),('guest_va',row['guest_va']+16)]:
  changed=dict(row,**{key:value});assert verify(pe,data,changed,pc,regs) is None;checks+=1
 for i in range(length):
  changed=bytearray(data);at=pe.get_offset_from_rva(START-pe.OPTIONAL_HEADER.ImageBase)+i;changed[at]^=1
  assert verify(pefile.PE(data=changed),bytes(changed),row,pc,regs) is None;checks+=1
assert verify(pe,data,row,0x10023c2c,regs) is None;checks+=1
row=next(r for r in json.loads((R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/result.json').read_text())['imports'] if r['name']=='SDL_GetDisplayForWindow');regs={'esi':row['guest_va']};pc=WINDOW_START+len(WINDOW_PREFIX)
assert verify(pe,data,row,pc,regs);checks+=1
for wrong in [{},{'esi':0},{'esi':row['guest_va']+16}]:assert verify(pe,data,row,pc,wrong) is None;checks+=1
for key,value in [('dll','libcurl.dll'),('name','SDL_GetPrimaryDisplay'),('slot',row['slot']+4),('guest_va',row['guest_va']+16)]:
 changed=dict(row,**{key:value});assert verify(pe,data,changed,pc,regs) is None;checks+=1
for i in range(len(WINDOW_PREFIX)):
 changed=bytearray(data);at=pe.get_offset_from_rva(WINDOW_START-pe.OPTIONAL_HEADER.ImageBase)+i;changed[at]^=1
 assert verify(pefile.PE(data=changed),bytes(changed),row,pc,regs) is None;checks+=1
assert verify(pe,data,row,pc-1,regs) is None;checks+=1
row=next(r for r in json.loads((R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/result.json').read_text())['imports'] if r['name']=='SDL_SyncWindow');regs={'esi':row['guest_va']}
assert verify(pe,data,row,0x1004a8a2,regs);checks+=1
assert verify(pe,data,dict(row,name='SDL_ShowWindow'),0x1004a8a2,regs) is None;checks+=1
assert verify(pe,data,row,0x1004a8a2,{'esi':row['guest_va']+16}) is None;checks+=1
miles=R/'ref/product/Mss32.dll';mdata=miles.read_bytes();mpe=pefile.PE(data=mdata)
mrow=next(r for r in json.loads((R/'generated/srw/run-20261004T204722-mss32/native-diagnostic/result.json').read_text())['imports'] if r['name']=='lstrcatA');mregs={'esi':mrow['guest_va']}
for pc in [0x21112142,0x21112149]:
 assert verify(mpe,mdata,mrow,pc,mregs);checks+=1
 for key,value in [('dll','user32.dll'),('name','lstrcpyA'),('slot',mrow['slot']+4),('guest_va',mrow['guest_va']+16)]:
  assert verify(mpe,mdata,dict(mrow,**{key:value}),pc,mregs) is None;checks+=1
 assert verify(mpe,mdata,mrow,pc,{'esi':0}) is None;checks+=1
 for i in range(pc-0x2111212d):
  changed=bytearray(mdata);changed[mpe.get_offset_from_rva(0x2111212d-mpe.OPTIONAL_HEADER.ImageBase)+i]^=1
  assert verify(pefile.PE(data=changed),bytes(changed),mrow,pc,mregs) is None;checks+=1
row=next(r for r in json.loads((R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/result.json').read_text())['imports'] if r['name']=='SDL_CreateSurfaceFrom');regs={'esi':row['guest_va']}
for pc in [0x100319e6,0x10031a17]:
 assert verify(pe,data,row,pc,regs);checks+=1
 for wrong in [{},{'esi':0},{'esi':row['guest_va']+16}]:assert verify(pe,data,row,pc,wrong) is None;checks+=1
 for key,value in [('dll','kernel32.dll'),('name','SDL_CreateSurface'),('slot',row['slot']+4),('guest_va',row['guest_va']+16)]:
  assert verify(pe,data,dict(row,**{key:value}),pc,regs) is None;checks+=1
 for i in range(pc-0x1003191b):
  changed=bytearray(data);changed[pe.get_offset_from_rva(0x1003191b-pe.OPTIONAL_HEADER.ImageBase)+i]^=1
  assert verify(pefile.PE(data=changed),bytes(changed),row,pc,regs) is None;checks+=1
out=R/'generated/integration-review/register-import-path';out.mkdir(exist_ok=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,checks=checks,image_sha256=hashlib.sha256(data).hexdigest(),sources_sha256={n:hashlib.sha256((R/n).read_bytes()).hexdigest() for n in ['scripts/register_import_path.py','scripts/test-register-import-path.py']},scope='Pinned original curl spans and exact adjacent MOV-IAT/CALL-register encoding on the pinned image, exact DLL/slot/token identity and native ESI. Rejects every tested byte mutation, missing/wrong register and wrong import metadata. No arbitrary backward disassembly or new callable imports.'),indent=2)+'\n');print('PASS:',checks,'pinned register-IAT validations/rejections')
