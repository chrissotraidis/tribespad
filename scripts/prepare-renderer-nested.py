#!/usr/bin/env python3
"""Compile the bounded renderer suffix preceding its 46-byte memset patch."""
import bisect, hashlib, json, pathlib, re
import pefile
R=pathlib.Path(__file__).resolve().parents[1]
P=R/'generated/integration-review/renderer-nested'; P.mkdir(exist_ok=True)
E=R/'generated/integration-review/renderer-dimensions/execution'
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll'
dll=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll'
image=R/'ref/control/Tribes2.exe'; p=pefile.PE(str(image))
s=engine.read_text(); d=dll.read_text(); sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def index(text):
 return {int(m[1],16):(m[1],text[m.start():text.index('\n}',m.start())+3]) for m in re.finditer(r'^define (?:hidden|protected) fastcc void @loc_([0-9A-F]+)\(',text,re.M)}
eblocks=index(s); dblocks=index(d); das=sorted(dblocks)
def raw(at,n):return p.get_data(at-0x400000,n)
def block(at,n):
 candidates=[(a,b) for a,(name,b) in eblocks.items() if a<=at and f';    store 0x{at+n:x}, esp, 4\n' in b]
 assert len(candidates)==1,(hex(at),len(candidates));return candidates[0]
audit=R/'generated/integration-review/renderer-batch/execution/nested-renderer-static-writes.json'
writes=[dict(pc=int(w['pc'],0),address=int(w['address'],0),size=w['size'],before=int.from_bytes(bytes.fromhex(w['before']),'little'),value=int(w['value'],0)) for w in json.loads(audit.read_text())['writes']]
for w in writes:assert raw(w['address'],w['size'])==w['before'].to_bytes(w['size'],'little')
records=[];variants=[];tails=[];targets=set()
# entry, changed instruction, original span end, kind, destination
specs=[(0x6b4a71,0x6b4a71,0x6b4a7f,'operand',0x6b4a94),(0x6b3848,0x6b3848,0x6b3851,'operand',0x6b3858),(0x6afd2d,0x6afd2d,0x6afd36,'erase',0x6afd36),(0x6b4e53,0x6b4e60,0x6b4e62,'branch',0x6b4eb5),(0x6b4a7f,0x6b4a87,0x6b4a8c,'call',0x1004c600),(0x6b4dc0,0x6b4dd7,0x6b4ddc,'call',0x1004c3a0),(0x6b4e74,0x6b4ea2,0x6b4ea7,'call',0x1004c5f0),(0x6b4eb5,0x6b4ec2,0x6b4ec6,'erasecmp',0x6b4eea),(0x6ab396,0x6ab3ae,0x6ab3b3,'call',0x1004c800),(0x6ab43a,0x6ab454,0x6ab459,'call',0x1004c800),(0x6b2a8a,0x6b2a90,0x6b2a95,'call',0x1004c360),(0x6b75d1,0x6b75e2,0x6b75e7,'call',0x1004c310),(0x6b0c00,0x6b0c09,0x6b0c0f,'stackcmp',0x6b0c21),(0x6b509e,0x6b50a6,0x6b50a8,'branch',0x6b50d6),(0x6b519d,0x6b51a5,0x6b51a7,'branch',0x6b51d5),(0x6a54b2,0x6a54b7,0x6a54bc,'nopcall',0x6a54bc),(0x6a55d1,0x6a55d3,0x6a55d8,'nopcall',0x6a55d8),(0x6b4c60,0x6b4c7f,0x6b4c81,'x87branch',0x6b4c90)]
for i,(entry,at,end,kind,target) in enumerate(specs):
 name,body=eblocks[entry];name='loc_'+name
 code=bytearray(raw(entry,end-entry))
 for w in writes:
  if entry<=w['address']<end:
   offset=w['address']-entry;code[offset:offset+w['size']]=w['value'].to_bytes(w['size'],'little')
 tail=f'musttail call fastcc void @loc_{target:X}(%_cpu* %cpu) nounwind\nret void\n}}\n'
 if kind=='call':
  old=at+5+int.from_bytes(raw(at+1,4),'little',signed=True);needle=f'@loc_{old:X}(';assert body.count(needle)==1
  changed=body.replace(needle,f'@loc_{target:X}(')
 elif kind=='operand':
  assert body.count('10164061')==1;changed=body.replace('10164061',str(0x1046cb8d))
 elif kind=='erase':changed=body[:body.index('\n')+1]+tail
 elif kind in ('branch','x87branch'):
  # Keep the original comparison and every flag spill, discard only its branch.
  marker=';and tmp1, eflags, '+('ZF' if at in (0x6b4e60,0x6b4c7f) else 'SF')
  if marker not in body:marker=';lshr tmp1, eflags, OF_SHIFT - SF_SHIFT'
  cut=body.rindex(marker);changed=body[:cut]+tail
 elif kind=='erasecmp':changed=body[:body.index(';and eflags')]+tail
 elif kind=='stackcmp':
  changed=body[:body.index(';add tmpadr, ebp, 20')]+ '%nested_flags = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 8\nstore i32 %57, i32* %nested_flags, align 4\n'+tail
 else:changed=body[:body.rindex(';    sub esp, esp, 4')]+tail
 variant=re.sub(r'%(\d+)',r'%v\1',changed);variants.append(variant)
 destinations=[int(n,16) for n in re.findall(r'musttail call fastcc void @loc_([0-9A-F]+)',changed)];targets.update(destinations)
 test=changed
 if kind=='call':
  test=changed[changed.rindex(';    sub esp, esp, 4'):];test=re.sub(r'%(\d+)',r'%v\1',test)
  live=set(re.findall(r'%v\d+',test))-set(re.findall(r'(%v\d+) =',test));initial=[]
  for v in sorted(live,key=lambda x:int(x[2:])):
   line=re.search(r'^'+re.escape(v.replace('%v','%'))+r' = (.*)$',body,re.M)[1]
   if 'i64' in line:typ,field,align='i64',13,8
   else:assert line.startswith(('sub i32 ','load i32 '));typ,field,align='i32',4,4
   initial.append(f'{v}_ptr = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 {field}\n{v} = load {typ}, {typ}* {v}_ptr, align {align}')
  test=f'define void @tested_{i}(%_cpu* %cpu) {{\n'+'\n'.join(initial)+'\n'+test
 elif kind=='x87branch':test=f'define void @tested_{i}(%_cpu* %cpu) {{\n'+tail
 else:test=re.sub(r'define (?:hidden|protected) fastcc void @'+name+r'\(',f'define void @tested_{i}(',variant, count=1)
 tails.append(test.replace('musttail call','call'))
 start=at if kind in ('call','x87branch') else entry
 records.append(dict(name=name,entry=entry,call=at,target=target,targets=destinations,kind=kind,start=start,after_bytes=bytes(code[start-entry:]).hex(),before_sha256=hashlib.sha256(body.encode()).hexdigest()))
# Pin exact original lifted store operands as well as instruction PCs. The first
# store in each writer block uses the guest pointer; CPU spills are distinct.
writes.sort(key=lambda w:w['pc'])
for w in writes:
 a=das[bisect.bisect_right(das,w['pc'])-1];name,body=dblocks[a]
 helper=re.search(r'^call void @x86_mov_mem_reg_(8|16|32)\(i32 (\d+), i32 (\d+)\) nounwind$',body,re.M)
 if helper:
  assert int(helper[1])==w['size']*8 and int(helper[2])==w['address'] and int(helper[3])==w['value']
  w.update(writer_name='loc_'+name,store=helper[0],absolute_helper=True,writer_sha256=hashlib.sha256(body.encode()).hexdigest());continue
 stores=list(re.finditer(r'^store i(8|16|32) ([^,]+), i\1\* (%\d+), align (\d+)$',body,re.M))
 guest=[]
 for m in stores:
  ptr=m[3];definition=re.search(r'^'+re.escape(ptr)+r' = (.*)$',body,re.M)[1]
  if definition.startswith('inttoptr '):guest.append(m)
 same=[x for x in writes if a<=x['pc'] and x['pc']< (das[bisect.bisect_right(das,a)] if bisect.bisect_right(das,a)<len(das) else 2**32)]
 assert len(guest)==len(same),(name,len(guest),len(same));m=guest[same.index(w)]
 assert int(m[1])==w['size']*8
 w.update(writer_name='loc_'+name,store=m[0],value_operand=m[2],pointer_operand=m[3],bits=int(m[1]),writer_sha256=hashlib.sha256(body.encode()).hexdigest())
prefix=s[:s.index('define ')];decls=''.join(f'declare hidden fastcc void @loc_{n:X}(%_cpu*)\n' for n in sorted(targets) if not re.search(r'^declare .*@loc_'+f'{n:X}'+r'\(',prefix,re.M))
for r in records:
 prefix=re.sub(r'^declare .*@'+r['name']+r'\([^\n]*\n','',prefix,flags=re.M)
 decls=re.sub(r'^declare .*@'+r['name']+r'\([^\n]*\n','',decls,flags=re.M)
(P/'engine.ll').write_text(prefix+decls+''.join(variants))
cpu=re.search(r'^%_cpu = type .*$',s,re.M)[0]
(P/'tails.ll').write_text(cpu+'\n'+''.join(f'declare hidden fastcc void @loc_{n:X}(%_cpu*)\n' for n in sorted(targets))+''.join(tails))
inputs=[engine,dll,image,audit,pathlib.Path(__file__).resolve()]
(P/'preparation.json').write_text(json.dumps(dict(passed=True,records=records,writes=writes,inputs_sha256={str(q.relative_to(R)):sha(q) for q in inputs},outputs_sha256={n:sha(P/n) for n in ['engine.ll','tails.ll']},scope='18 finite renderer variants and 22 exact stores; original prefixes retained except overwritten comparisons and CALLs.'),indent=2)+'\n')
print('Prepared 18 variants and 22 exact stores')
