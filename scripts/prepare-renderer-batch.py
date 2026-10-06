#!/usr/bin/env python3
"""Compile the bounded renderer suffix preceding its 46-byte memset patch."""
import bisect, hashlib, json, pathlib, re
import pefile
R=pathlib.Path(__file__).resolve().parents[1]
P=R/'generated/integration-review/renderer-batch'; P.mkdir(exist_ok=True)
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
# Explicit direct E8 preimages select the original direct-store branches only.
calls=[{k:int(v,0) for k,v in row.items()} for row in json.loads((E/'remaining-calls-static.json').read_text())['calls'][:13]]
nops=json.loads((E/'remaining-nops-static.json').read_text())['ranges'][:5]
records=[];writes=[];variants=[];tails=[];targets=set()
for i,row in enumerate(calls):
 at=row['call'];old=row['old_target'];new=row['target'];b=raw(at,5)
 assert b[0]==0xe8 and at+5+int.from_bytes(b[1:],'little',signed=True)==old
 entry,body=block(at,5);name=f'loc_{entry:X}';needle=f'@loc_{old:X}('
 assert body.count(needle)==1;changed=body.replace(needle,f'@loc_{new:X}(');variants.append(changed);targets.add(new)
 tail=changed[changed.rindex(';    sub esp, esp, 4'):];tail=re.sub(r'%(\d+)',r'%v\1',tail)
 live=set(re.findall(r'%v\d+',tail))-set(re.findall(r'(%v\d+) =',tail));initial=[]
 for v in sorted(live,key=lambda x:int(x[2:])):
  line=re.search(r'^'+re.escape(v.replace('%v','%'))+r' = (.*)$',body,re.M)[1]
  if 'i64' in line:typ,field,align='i64',13,8
  else:assert line.startswith('sub i32 ');typ,field,align='i32',4,4
  initial.append(f'{v}_ptr = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 {field}\n{v} = load {typ}, {typ}* {v}_ptr, align {align}')
 tails.append(f'define void @tested_{i}(%_cpu* %cpu) {{\n'+'\n'.join(initial)+'\n'+tail.replace('musttail call','call'))
 records.append(dict(name=name,entry=entry,call=at,size=5,target=new,kind='call',after_bytes=(b'\xe8'+row['value'].to_bytes(4,'little')).hex(),before_sha256=hashlib.sha256(body.encode()).hexdigest()))
 writes.append(dict(pc=row['writer'],address=at+1,size=4,before=int.from_bytes(b[1:],'little'),value=row['value']))
# NOP variants preserve the prefix and its deferred ESP; remove the original
# CALL return push, dispatch and (for indirect CALL) target load. No callee pop.
for i,(row,last_pc) in enumerate(zip(nops,[0x10047a4e,0x10047df8,0x10047e1a,0x10047e59,0x10047e7c])):
 at=int(row['address'],0);n=row['size'];b=raw(at,n);assert b.hex()==row['before']
 entry,body=block(at,n);name=f'loc_{entry:X}';callstart=body.rindex(';    sub esp, esp, 4')
 sub=re.search(r'^%\d+ = sub i32 (%\d+), 4$',body[callstart:],re.M);assert sub
 esp=sub[1];prefix=body[:callstart]
 if n==6:
  assert ';mov tmp19, 0x9e5e88\n' in prefix;prefix=prefix[:prefix.index(';mov tmp19, 0x9e5e88\n')]
 assert esp in prefix
 changed=prefix+f'%nop_esp = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 4\nstore i32 {esp}, i32* %nop_esp, align 4\nmusttail call fastcc void @loc_{at+n:X}(%_cpu* %cpu) nounwind\nret void\n}}\n'
 changed=re.sub(r'%(\d+)',r'%v\1',changed);variants.append(changed);targets.add(at+n)
 # These single original instructions have no historical translated entry inside.
 assert not any(at<=a<at+n for a in eblocks)
 records.append(dict(name=name,entry=entry,call=at,size=n,target=at+n,kind='nop',before_sha256=hashlib.sha256(body.encode()).hexdigest(),before_block=raw(entry,at+n-entry).hex(),after_block=(raw(entry,at-entry)+b'\x90'*n).hex()))
 for pc,offset,size in [(int(row['writer_or_memset_call'],0),0,4),(last_pc,4,n-4)]:
  writes.append(dict(pc=pc,address=at+offset,size=size,before=int.from_bytes(b[offset:offset+size],'little'),value=int.from_bytes(b'\x90'*size,'little')))
# Pin exact original lifted store operands as well as instruction PCs. The first
# store in each writer block uses the guest pointer; CPU spills are distinct.
writes.sort(key=lambda w:w['pc'])
for w in writes:
 a=das[bisect.bisect_right(das,w['pc'])-1];name,body=dblocks[a]
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
(P/'engine.ll').write_text(prefix+decls+''.join(variants))
cpu=re.search(r'^%_cpu = type .*$',s,re.M)[0]
(P/'tails.ll').write_text(cpu+'\n'+''.join(f'declare hidden fastcc void @loc_{n:X}(%_cpu*)\n' for n in sorted(targets))+''.join(tails)+''.join(v.replace('define hidden fastcc void @'+r['name']+'(',f'define void @tested_{i}(',1).replace('musttail call','call') for i,(r,v) in enumerate(zip(records,variants)) if r['kind']=='nop'))
inputs=[engine,dll,image,E/'remaining-calls-static.json',E/'remaining-nops-static.json',pathlib.Path(__file__).resolve()]
(P/'preparation.json').write_text(json.dumps(dict(passed=True,records=records,writes=writes,inputs_sha256={str(q.relative_to(R)):sha(q) for q in inputs},outputs_sha256={n:sha(P/n) for n in ['engine.ll','tails.ll']},scope='13 CALL redirects plus five single CALL removals, 23 ordered stores before the original memset boundary. CALL prefixes identical; removed calls retain prefix ESP and no return push or callee effects.'),indent=2)+'\n')
print('Prepared 18 variants and 23 exact stores')
