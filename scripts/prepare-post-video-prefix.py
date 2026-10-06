#!/usr/bin/env python3
"""Prepare, but do not publish, the first two post-video engine variants."""
import hashlib,json,pathlib,re,struct
import capstone
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
frozen=R/'generated/integration-review/curl-threads-checkpoint-20261003';capture=frozen/'generated/integration-review/post-video';out=R/'generated/integration-review/post-video-prefix';out.mkdir(exist_ok=True)
checkpoint=json.loads((frozen/'checkpoint.json').read_text());meta=json.loads((capture/'main-capture.json').read_text());assert meta['pc']=='0x1005b384'
for name in ['main-capture.json','main-cpu.bin','main-005bb000.bin','main-00561000.bin','main-1005b000.bin']:
 p=capture/name;assert sha(p)==checkpoint['files'][str(p.relative_to(frozen))]
def read(a,n):
 for row in meta['ranges']:
  if row['address']<=a and a+n<=row['address']+row['size']:
   p=capture/row['file'];assert sha(p)==row['sha256'];return p.read_bytes()[a-row['address']:a-row['address']+n]
 raise AssertionError(hex(a))
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';text=engine.read_text();md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
variants={};rows=[]
for name,entry,end,pc,address,size,value in [('loc_5BB95E',0x5bb95e,0x5bb979,0x1005b384,0x5bb96b,4,0x1004ae80),('loc_561683',0x561683,0x56168a,0x1005b3a1,0x561689,1,0x5b)]:
 raw=read(entry,end-entry);patched=bytearray(raw);at=address-entry;old=bytes(patched[at:at+size]);patched[at:at+size]=value.to_bytes(size,'little')
 before=list(md.disasm(raw,entry));after=list(md.disasm(bytes(patched),entry));assert len(before)==len(after)
 changed=[(a,b) for a,b in zip(before,after) if a.bytes!=b.bytes];assert len(changed)==1
 a,b=changed[0]
 match=re.search(r'^define hidden fastcc void @'+name+r'\([^\n]*\n.*?^}\n',text,re.M|re.S);assert match;body=match[0]
 if size==4:
  assert a.mnemonic==b.mnemonic=='push' and a.operands[0].imm==0x5bacd0 and b.operands[0].imm==value
  assert body.count('store i32 6008016,')==1;variant=body.replace('store i32 6008016,','store i32 %d,'%value).replace(';    store 0x5bacd0, esp, 4',';    store 0x1004ae80, esp, 4')
 else:
  assert a.mnemonic==b.mnemonic=='jmp' and a.operands[0].imm==0x561695 and b.operands[0].imm==0x5616e5
  assert body.count('@loc_561695(')==1;variant=body.replace('@loc_561695(','@loc_5616E5(').replace(';tcall loc_561695',';tcall loc_5616E5')
 assert variant!=body;variants[name]=variant
 writer=list(md.disasm(read(pc,10 if size==4 else 7),pc));assert len(writer)==1 and writer[0].mnemonic=='mov' and writer[0].operands[0].mem.disp==address and writer[0].operands[1].imm==value
 rows.append(dict(name=name,entry=entry,end=end,before_bytes=raw.hex(),after_bytes=bytes(patched).hex(),old_definition_sha256=hashlib.sha256(body.encode()).hexdigest(),new_definition_sha256=hashlib.sha256(variant.encode()).hexdigest(),write=dict(pc=pc,address=address,size=size,before=old.hex(),after=value.to_bytes(size,'little').hex())))
pre=text[:text.index('define ')];decl='\n'.join('declare hidden fastcc void @%s(%%_cpu*)'%n for n in ['loc_426590','loc_5616E5'])
(out/'engine.ll').write_text(pre+decl+'\n'+''.join(variants.values()))
report=dict(passed=True,published=False,records=rows,inputs_sha256={str(p.relative_to(R)):sha(p) for p in [engine,frozen/'checkpoint.json',pathlib.Path(__file__).resolve()]},outputs_sha256={'engine.ll':sha(out/'engine.ll')},scope='Candidate only: callback PUSH 0x5bacd0->0x1004ae80 and short JMP 0x561695->0x5616e5 from captured preimages. No write admitted or14th revision. Original main later registers a separate callback at0x426410, so publication must precede that consumer; intervening VirtualProtect must actually succeed. New callback service behavior and subsequent patch batch remain unverified.')
(out/'preparation.json').write_text(json.dumps(report,indent=2)+'\n');print('Prepared two unpublished post-video variants from exact captured code bytes')
