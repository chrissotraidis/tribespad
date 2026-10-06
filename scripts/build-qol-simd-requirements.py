#!/usr/bin/env python3
"""Validate observed startup SIMD sites against pinned PE bytes for AOT planning."""
import hashlib,json,pathlib
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
images=[]
for name in ('ref/control/Tribes2.exe','ref/product/IFC22.dll'):
 path=R/name;pe=pefile.PE(str(path));images.append((name,pe.OPTIONAL_HEADER.ImageBase,pe.get_memory_mapped_image(),sha(path)))
profiles={};families={}
for name,sse2 in [('no-sse2',False),('sse2',True)]:
 path=R/f'generated/qol-initialization/executable-capture-{name}.json';capture=json.loads(path.read_text())
 assert capture['state']=='finished' and capture['initialized'] and capture['cpu_profile']['sse2']==sse2
 assert capture['script_sha256']==sha(R/'scripts/trace-qol-initialization.py')
 assert capture['instruction_cache_reference']['contract_source_sha256']==sha(R/'scripts/guest_cache_reference.py')
 assert not capture['executable_reference']['executed_instructions']
 seen=set();completed={}
 for site in capture['simd_coverage']['sites']:
  pc=site['pc'];raw=bytes.fromhex(site['bytes_hex']);key=(pc,raw);assert key not in seen;seen.add(key)
  assert site['entered']==site['completed']+site['unconfirmed'] and site['unconfirmed']==0
  ins=next(decoder.disasm(raw,pc));assert ins.size==len(raw) and ins.mnemonic==site['mnemonic'] and ins.op_str==site['operands']
  owner=next(image for image in images if image[1]<=pc<image[1]+len(image[2]))
  offset=pc-owner[1];assert owner[2][offset:offset+len(raw)]==raw,'observed SIMD differs from pinned original PE bytes'
  completed[ins.mnemonic]=completed.get(ins.mnemonic,0)+site['completed']
  family=families.setdefault(ins.mnemonic,{'sites':{},'completed_by_profile':{}})
  family['sites'][key]={'image':owner[0],'pc':pc,'bytes_hex':raw.hex(),'operands':ins.op_str}
 assert completed==capture['simd_coverage']['completed_by_mnemonic']
 for mnemonic,count in completed.items():families[mnemonic]['completed_by_profile'][name]=count
 profiles[name]={'capture_sha256':sha(path),'initialized':True,'sites':len(seen),'completed_instructions':sum(completed.values()),'completed_by_mnemonic':completed}
rows=[{'mnemonic':name,'sites':sorted(family['sites'].values(),key=lambda site:site['pc']),'completed_by_profile':family['completed_by_profile']} for name,family in sorted(families.items())]
report={'state':'finished','driver_sha256':sha(pathlib.Path(__file__).resolve()),'tracer_sha256':sha(R/'scripts/trace-qol-initialization.py'),'image_sha256':{image[0]:image[3] for image in images},'profiles':profiles,'families':rows,'scope':'Observed complete original-x86 startup SIMD requirements, independently decoded and matched to pinned PE bytes. Unobserved SIMD/VEX paths still require static translation or explicit boundaries. No native ARM64 support, MXCSR parity, rendering or gameplay claim.'}
out=R/'generated/qol-simd-requirements.json';out.write_text(json.dumps(report,indent=2)+'\n')
print(f'Validated {len(rows)} SIMD families across both completed startup captures; native lowering remains required')
