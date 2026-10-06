#!/usr/bin/env python3
"""Execute SRW-emitted SSE move bodies from pinned original QoL bytes on ARM64.
Isolated PE fixtures seed each instruction without translating the whole DLL.
Only harness labels/export convention and the guest RET are replaced.
"""
import argparse,hashlib,json,os,pathlib,random,re,struct,subprocess
import capstone,pefile,unicorn
from capstone import x86_const as C
from unicorn import x86_const as X
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');ap.add_argument('--sd',action='store_true',help='Legacy scalar MOVSD collision regression');ap.add_argument('--scalar',action='store_true',help='Also test ADDSD/SUBSD/MULSS emitted state and SoftFloat TLS');ap.add_argument('--pd2ps',action='store_true',help='Also test packed double-to-single rounding and exceptions');ap.add_argument('--truncate',action='store_true',help='Also test CVTTSS2SI with the Rosetta integer/MXCSR reference');ap.add_argument('--compare',action='store_true',help='Also test UCOMISD with the Rosetta MXCSR/flags reference');ap.add_argument('--dq2pd',action='store_true',help='Also test exact signed integer-to-double conversion');ap.add_argument('--packed',action='store_true',help='Also test legacy packed/bitwise instructions');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
out=R/('generated/sse/lowering-sd' if a.sd else 'generated/sse/lowering-scalar' if a.scalar else 'generated/sse/lowering-pd2ps' if a.pd2ps else 'generated/sse/lowering-truncate' if a.truncate else 'generated/sse/lowering-compare' if a.compare else 'generated/sse/lowering-dq2pd' if a.dq2pd else 'generated/sse/lowering-packed' if a.packed else 'generated/sse/lowering')/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
lock=json.loads((R/'toolchains.lock.json').read_text());build=json.loads((R/'generated/tools/SRW-tribes/tribes-build.json').read_text());srw=R/'generated/tools/SRW-tribes/SRW.exe';assert build['patch_sha256']==lock['tribes_extensions']['patch_sha256']==sha(R/'scripts/patches/srw-tribes.patch');assert build['srw_sha256']==sha(srw)
requirements=json.loads((R/'generated/qol-simd-requirements.json').read_text());original=R/'ref/product/IFC22.dll';assert sha(original)==requirements['image_sha256']['ref/product/IFC22.dll'];pe=pefile.PE(str(original));image=pe.get_memory_mapped_image();decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
names=set('movss movlpd movq movd movups movdqu movaps'.split());sites=[]
if a.packed:names.update('xorps orps pcmpeqw pmovmskb pshufd pshuflw'.split())
if a.dq2pd:names.add('cvtdq2pd')
if a.compare:names.add('ucomisd')
if a.truncate:names.add('cvttss2si')
if a.pd2ps:names.add('cvtpd2ps')
if a.scalar:names.update(['addsd','subsd','mulss'])
for family in requirements['families']:
 if family['mnemonic'] not in names:continue
 for site in family['sites']:
  raw=bytes.fromhex(site['bytes_hex']);assert site['image']=='ref/product/IFC22.dll' and image[site['pc']-pe.OPTIONAL_HEADER.ImageBase:site['pc']-pe.OPTIONAL_HEADER.ImageBase+len(raw)]==raw
  ins=next(decoder.disasm(raw,site['pc']));assert ins.mnemonic==family['mnemonic'];sites.append({'pc':site['pc'],'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'pinned original QoL'})
if a.sd:
 names={'movsd'};sites=[]
 # Executable-section linear census is supplemental: test every discovered
 # scalar form plus exhaustive register roles and synthetic addressing below.
 scan=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);scan.skipdata=True
 for section in pe.sections:
  if not section.Characteristics&0x20000000:continue
  for pc,size,mnemonic,operands in scan.disasm_lite(section.get_data(),pe.OPTIONAL_HEADER.ImageBase+section.VirtualAddress):
   if mnemonic=='movsd' and 'xmm' in operands:
    sites.append(dict(pc=pc,bytes_hex=pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,size).hex(),assembly=mnemonic+' '+operands,origin='pinned original QoL executable-section census'))
 assert any(s['pc']==0x1007f679 for s in sites)
original_sites=len(sites)
# Add cold operand roles and SIB/displacement forms that startup did not cover.
for name,load,store in ([('movsd','f20f10','f20f11')] if a.sd else [('movss','f30f10','f30f11'),('movlpd','660f12','660f13'),('movq','f30f7e','660fd6'),('movd','660f6e','660f7e'),('movups','0f10','0f11'),('movdqu','f30f6f','f30f7f'),('movaps','0f28','0f29')]):
 for index in range(8):
  for prefix in (load,store):
   # [ebx + esi*4 - 17], [esp + 0x23], [eax].
   for encoded in (bytes([0x44|(index<<3),0xb3,0xef]),bytes([0x44|(index<<3),0x24,0x23]),bytes([index<<3])):
    raw=bytes.fromhex(prefix)+encoded;ins=next(decoder.disasm(raw,0));assert ins.mnemonic==name;sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic operand coverage'})
  if name!='movlpd':
   for src in range(8):
    raw=bytes.fromhex(load)+bytes([0xc0|(index<<3)|src]);ins=next(decoder.disasm(raw,0));assert ins.mnemonic==name;sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic register coverage'})
if a.sd:
 # Opcode11 reverses the encoded register roles; cover every alias pairing.
 for dest in range(8):
  for source in range(8):
   raw=bytes.fromhex('f20f11')+bytes([0xc0|(source<<3)|dest]);ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='movsd';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic reverse register opcode coverage'})
if a.packed:
 for name,prefix in [('xorps','0f57'),('orps','0f56'),('pcmpeqw','660f75')]:
  for dest in range(8):
   for source in range(8):
    raw=bytes.fromhex(prefix)+bytes([0xc0|(dest<<3)|source]);ins=next(decoder.disasm(raw,0));assert ins.mnemonic==name;sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic packed register coverage'})
   for encoded in (bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([dest<<3])):
    raw=bytes.fromhex(prefix)+encoded;ins=next(decoder.disasm(raw,0));assert ins.mnemonic==name;sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic packed memory coverage'})
 for dest in range(8):
  for source in range(8):
   raw=bytes.fromhex('660fd7')+bytes([0xc0|(dest<<3)|source]);ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='pmovmskb';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic byte-mask coverage'})
 for name,prefix in [('pshufd','660f70'),('pshuflw','f20f70')]:
  variants=[]
  for dest in range(8):
   for source in range(8):
    for control in (0,0x1b,0xe4,0xff):variants.append(bytes([0xc0|(dest<<3)|source,control]))
   for control in (0,0x1b,0xe4,0xff):
    for encoded in (bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([dest<<3])):variants.append(encoded+bytes([control]))
  # Exhaust every immediate control on two distinct registers.
  variants.extend(bytes([0xc1,control]) for control in range(256))
  for encoded in variants:
   raw=bytes.fromhex(prefix)+encoded;ins=next(decoder.disasm(raw,0));assert ins.mnemonic==name;sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic shuffle/control coverage'})
if a.dq2pd:
 for dest in range(8):
  for source in range(8):
   raw=bytes.fromhex('f30fe6')+bytes([0xc0|(dest<<3)|source]);ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='cvtdq2pd';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic exact conversion register coverage'})
  for encoded in (bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([dest<<3])):
   raw=bytes.fromhex('f30fe6')+encoded;ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='cvtdq2pd';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic exact conversion memory coverage'})
if a.compare:
 for dest in range(8):
  for source in range(8):
   raw=bytes.fromhex('660f2e')+bytes([0xc0|(dest<<3)|source]);ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='ucomisd';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic compare register/self-alias coverage'})
  for encoded in (bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([dest<<3])):
   raw=bytes.fromhex('660f2e')+encoded;ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='ucomisd';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic compare memory coverage'})
if a.truncate:
 for dest in range(8):
  for source in range(8):
   raw=bytes.fromhex('f30f2c')+bytes([0xc0|(dest<<3)|source]);ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='cvttss2si';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic truncation register coverage'})
  for encoded in (bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([dest<<3])):
   raw=bytes.fromhex('f30f2c')+encoded;ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='cvttss2si';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic truncation memory coverage'})
if a.pd2ps:
 for dest in range(8):
  for source in range(8):
   raw=bytes.fromhex('660f5a')+bytes([0xc0|(dest<<3)|source]);ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='cvtpd2ps';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic packed conversion register/self-alias coverage'})
  for encoded in (bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([dest<<3])):
   raw=bytes.fromhex('660f5a')+encoded;ins=next(decoder.disasm(raw,0));assert ins.mnemonic=='cvtpd2ps';sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic packed conversion memory coverage'})
if a.scalar:
 for name,prefix in [('addsd','f20f58'),('subsd','f20f5c'),('mulss','f30f59')]:
  for dest in range(8):
   for source in range(8):
    raw=bytes.fromhex(prefix)+bytes([0xc0|(dest<<3)|source]);ins=next(decoder.disasm(raw,0));assert ins.mnemonic==name;sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic scalar arithmetic register/self-alias coverage'})
   for encoded in (bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([dest<<3])):
    raw=bytes.fromhex(prefix)+encoded;ins=next(decoder.disasm(raw,0));assert ins.mnemonic==name;sites.append({'pc':None,'bytes_hex':raw.hex(),'assembly':ins.mnemonic+' '+ins.op_str,'origin':'synthetic scalar arithmetic memory coverage'})
def fixture(directory,records):
 directory.mkdir(parents=True,exist_ok=True);raw=bytearray(original.read_bytes()[:0x400]);header=struct.unpack_from('<I',raw,0x3c)[0];optional=header+24;code=bytearray();aliases=[]
 for i,site in enumerate(records):
  instruction=bytes.fromhex(site['bytes_hex']);assert len(instruction)<31;code+=instruction+b'\xc3'+bytes(31-len(instruction));aliases.append(f'loc_{0x10001000+32*i:X},fixture_{i}')
 span=(len(code)+511)&~511;struct.pack_into('<H',raw,header+6,1);struct.pack_into('<H',raw,header+22,0x102);struct.pack_into('<I',raw,optional+4,span);struct.pack_into('<I',raw,optional+16,0x1000);struct.pack_into('<I',raw,optional+56,(0x1000+span+4095)&~4095);struct.pack_into('<I',raw,optional+60,0x400);raw[optional+96:optional+224]=bytes(128);section=optional+224;raw[section:]=bytes(len(raw)-section);struct.pack_into('<8s6I2HI',raw,section,b'.text\0\0\0',len(code),0x1000,span,0x400,0,0,0,0,0x60000020);raw+=code+bytes(span-len(code));(directory/'fixture.exe').write_bytes(raw);(directory/'relocations.csv').write_text('');(directory/'global_aliases.sci').write_text('\n'.join(aliases)+'\n')
 decoded=pefile.PE(data=bytes(raw));assert decoded.OPTIONAL_HEADER.ImageBase==0x10000000
 for i,site in enumerate(records):assert decoded.get_data(0x1000+32*i,len(bytes.fromhex(site['bytes_hex']))).hex()==site['bytes_hex']
 run=subprocess.run([str(srw),'fixture.exe','fixture.llasm'],cwd=directory,env={k:v for k,v in {**os.environ,'TRIBES_SRW_FULL_FLAGS':'1'}.items() if k!='HALOPAD_SRW_DIAG'},capture_output=True,text=True,timeout=60);(directory/'srw.log').write_text(run.stdout+run.stderr);return run
run=fixture(out/'fixture',sites);assert not run.returncode,run.stderr
text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(sites)
bodies={}
for match in procedures:
 index=int(match[1]);parts=match[2].split(';ret\n');assert len(parts)==2
 body=parts[0].strip();assert body.startswith(';') and 'call tb_sse_' in body and 'unknown' not in body
 bodies[index]=body
llasm=out/'blocks.llasm';llasm.write_text('include tribes_sse.llinc\n'+''.join(f'proc test_{i} public\n{bodies[i]}\nendp\n' for i in range(len(sites))))
ir_path=out/'blocks.ll';run=subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-o',str(ir_path),str(llasm)],capture_output=True,text=True);(out/'llasm.log').write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
ir=ir_path.read_text();assert ir.count('define hidden fastcc void @test_')==len(sites);ir_path.write_text(ir.replace('define hidden fastcc void @test_','define ccc void @test_'))
negative={}
for name,raw in ([('locked-movsd','f0f20f1100'),('segmented-movsd','64f20f1000'),('address16-movsd','67f20f1000')] if a.sd else [('locked-divss','f0f30f5ec1')]):
 run=fixture(out/name,[{'bytes_hex':raw}]);assert run.returncode!=0 and 'Error:' in run.stderr;negative[name]={'exit':run.returncode,'state':'explicit strict translation failure'}
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x1000,4096);u.mem_map(0x10000000,0x500000);rng=random.Random(25034);regs=[getattr(X,'UC_X86_REG_'+n) for n in ('EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI')];regindex={name:i for i,name in enumerate(('eax','ecx','edx','ebx','esp','ebp','esi','edi'))};cases=[];compare_requests=[];compare_indices=[]
for index,site in enumerate(sites):
 ins=next(decoder.disasm(bytes.fromhex(site['bytes_hex']),0x1000))
 for iteration in range(320 if ins.mnemonic in {'addsd','subsd','mulss'} else 704 if ins.mnemonic=='cvtpd2ps' else 576 if ins.mnemonic=='cvttss2si' else 128 if ins.mnemonic=='ucomisd' else 16 if ins.mnemonic=='cvtdq2pd' else 8):
  general=[rng.getrandbits(32) for _ in range(8)];flags=0x202|rng.choice([0,0x8d5,0x41,0x884]);initial=bytes(rng.getrandbits(8) for _ in range(128))
  if a.sd:flags|=(iteration&1)<<10
  if iteration==0:initial=bytes(128)
  elif iteration==1:initial=b'\xff'*128
  elif iteration==2:initial=bytes(range(16))*8
  if ins.mnemonic=='pcmpeqw' and iteration==2 and ins.operands[1].type==C.X86_OP_REG:
   dest=int(ins.reg_name(ins.operands[0].reg)[3:]);source=int(ins.reg_name(ins.operands[1].reg)[3:])
   if dest!=source:
    mixed=bytearray(initial)
    for lane in range(1,8,2):mixed[source*16+lane*2]^=1
    initial=bytes(mixed)
  if ins.mnemonic=='cvtdq2pd':
   edges=[0,1,0xffffffff,0x80000000,0x7fffffff,0x01000001,0x80000001,0xffff0001]
   first=edges[iteration%8] if iteration<8 else rng.getrandbits(32)
   second=edges[(iteration+3)%8] if iteration<8 else rng.getrandbits(32)
   pair=struct.pack('<2I',first,second)
   initial=b''.join(pair+initial[i*16+8:i*16+16] for i in range(8))
  if ins.mnemonic=='ucomisd':
   edges=[0,0x8000000000000000,0x3ff0000000000000,0xbff0000000000000,0x7ff0000000000000,0xfff0000000000000,0x7ff8000000000001,0xfff8000000000123,0x7ff0000000000001,0xfff0000000000123,1,0x8000000000000001,0x000fffffffffffff,0x0010000000000000,0x7fefffffffffffff,0xffefffffffffffff]
   left_value=edges[(iteration//4)%16] if iteration<64 else rng.getrandbits(64)
   right_value=edges[((iteration//4)*7+3)%16] if iteration<64 else rng.getrandbits(64)
   vector=bytearray(initial)
   left_index=int(ins.reg_name(ins.operands[0].reg)[3:]);vector[left_index*16:left_index*16+8]=struct.pack('<Q',left_value)
   if ins.operands[1].type==C.X86_OP_REG:
    right_index=int(ins.reg_name(ins.operands[1].reg)[3:]);vector[right_index*16:right_index*16+8]=struct.pack('<Q',right_value)
   initial=bytes(vector)
  if ins.mnemonic=='cvttss2si':
   edges=[0,0x80000000,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc00001,0xffc00123,0x7f800001,0xff800123,1,0x80000001,0x007fffff,0x00800000,0x7f7fffff,0x4f000000,0xcf000000,0x4effffff,0xceffffff,0xcf000001,0x3fc00000,0xbfc00000,0x3f7fffff,0xbf7fffff,0x4b000001,0xcb000001,0x4b800001,0xcb800001,0x00800001,0x80800001,0x7fffffff,0xffffffff]
   source_value=edges[iteration//16] if iteration<512 else rng.getrandbits(32)
   if ins.operands[1].type==C.X86_OP_REG:
    source_index=int(ins.reg_name(ins.operands[1].reg)[3:]);vector=bytearray(initial);vector[source_index*16:source_index*16+4]=struct.pack('<I',source_value);initial=bytes(vector)
  if ins.mnemonic=='cvtpd2ps':
   edges=[0,0x8000000000000000,0x3ff0000000000000,0xbff0000000000000,0x7ff0000000000000,0xfff0000000000000,0x7ff8000000000001,0xfff8000000000123,0x7ff0000000000001,0xfff0000000000123,1,0x8000000000000001,0x000fffffffffffff,0x0010000000000000,0x7fefffffffffffff,0xffefffffffffffff,0x380fffffe0000000,0xb80fffffe0000000,0x3810000000000000,0x36a0000000000000,0xb6a0000000000000,0x3690000000000000,0x3690000000000001,0x47efffffe0000000,0x47effffff0000000,0x47f0000000000000,0xc7f0000000000000,0x3ff0000010000000,0x3ff0000030000000,0xbff0000010000000,0xbff0000030000000,0x36a8000000000000,0x380ffffff0000000,0xb80ffffff0000000,0x380fffffefffffff,0xb80fffffefffffff,0x380fffffffffffff,0xb80fffffffffffff,0x380fffffe0000001,0xb80fffffe0000001]
   lane0=edges[iteration//16] if iteration<640 else rng.getrandbits(64)
   partner={6:10,7:11,8:10,9:11}.get(iteration//16,((iteration//16)*7+3)%40)
   lane1=edges[partner] if iteration<640 else rng.getrandbits(64)
   if 640<=iteration<644:
    # Retained high NaN payload bits, not only payloads truncated to zero.
    lane0=[0x7ffa5a5a12345678,0xfffa5a5a12345678,0x7ff12345abcdeffe,0xfff12345abcdeffe][iteration-640]
    lane1=lane0^0x8000000000000000
   packed_source=struct.pack('<2Q',lane0,lane1)
   if ins.operands[1].type==C.X86_OP_REG:
    source_index=int(ins.reg_name(ins.operands[1].reg)[3:]);vector=bytearray(initial);vector[source_index*16:source_index*16+16]=packed_source;initial=bytes(vector)
  if ins.mnemonic in {'addsd','subsd','mulss'}:
   pairs64=[(0,0x8000000000000000),(0x3ff0000000000000,0xbff0000000000000),(0x0010000000000000,0x800fffffffffffff),(1,0x3ff0000000000000),(0x7ff0000000000000,0xfff0000000000000),(0x7ffa5a5a12345678,0xfff12345abcdeffe),(0x7ff12345abcdeffe,0xfffa5a5a12345678),(1,0x7ff0000000000000),(0x7fefffffffffffff,0x7fefffffffffffff),(0xffefffffffffffff,0xffefffffffffffff),(0x0010000000000000,0x8010000000000000),(0x7ffa5a5a12345678,1),(0x0010000000000001,0x8010000000000000),(0x7fefffffffffffff,1),(0x3ff0000000000000,0x3ca0000000000000),(0xbff0000000000000,0xbca0000000000000)]
   pairs32=[(0x00800000,0x3f7fffff),(0x00800001,0x3f7ffffe),(1,0x3f800000),(0x007fffff,0x3f800000),(0x7f800000,0),(0xff800000,0x80000000),(0x7fc12345,0xffa56789),(0x7fa12345,0xffc56789),(0x7fc12345,1),(0x7f7fffff,0x40000000),(0xff7fffff,0x40000000),(0x3fc00000,0x3fc00000),(0x80000000,0xbf800000),(1,0x3f000000),(0x00800002,0x3f7ffffe),(0x00800000,0xbf7fffff)]
   n=4 if ins.mnemonic=='mulss' else 8;pairs=pairs32 if n==4 else pairs64
   a_value,b_value=pairs[iteration//16] if iteration<256 else (rng.getrandbits(n*8),rng.getrandbits(n*8))
   left_index=int(ins.reg_name(ins.operands[0].reg)[3:]);vector=bytearray(initial);vector[left_index*16:left_index*16+n]=a_value.to_bytes(n,'little')
   if ins.operands[1].type==C.X86_OP_REG:
    source_index=int(ins.reg_name(ins.operands[1].reg)[3:]);vector[source_index*16:source_index*16+n]=b_value.to_bytes(n,'little')
   initial=bytes(vector);scalar_memory=b_value.to_bytes(n,'little')
  mxcsr=0x1f80|((iteration%4)<<13)|(0x8040 if iteration&4 else 0)|(0x3f if iteration&8 else 0)
  if ins.mnemonic in {'ucomisd','cvttss2si','cvtpd2ps','addsd','subsd','mulss'}:mxcsr=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(0x3f if iteration&16 else 0)
  # Keep boundary cases' status clear so preexisting IE/PE cannot hide a
  # missing exception. Random cases separately check sticky preservation.
  if (ins.mnemonic=='cvttss2si' and iteration<512) or (ins.mnemonic=='cvtpd2ps' and iteration<640):mxcsr&=~0x3f
  if ins.mnemonic in {'addsd','subsd','mulss'} and iteration<256:mxcsr&=~0x3f
  address=0x10200003;size=0;memory=bytes(16)
  for op in ins.operands:
   if op.type!=C.X86_OP_MEM:continue
   mem=op.mem;base=ins.reg_name(mem.base);ind=ins.reg_name(mem.index);wanted=0x10200020 if ins.mnemonic in {'movaps','xorps','orps','pcmpeqw','pshufd','pshuflw','cvtpd2ps'} else 0x10200003
   if base:
    if ind:general[regindex[ind]]=rng.randrange(1024)
    general[regindex[base]]=(wanted-mem.disp-(general[regindex[ind]]*mem.scale if ind else 0))&0xffffffff
   elif ind:general[regindex[ind]]=(wanted-mem.disp)//mem.scale
   address=(mem.disp+(general[regindex[base]] if base else 0)+(general[regindex[ind]]*mem.scale if ind else 0))&0xffffffff;size=op.size;assert 0x10000000<=address<0x10500000-size and 0<size<=16
   memory=bytes(rng.getrandbits(8) for _ in range(16))
   if iteration==0:memory=bytes(16)
   elif iteration==1:memory=b'\xff'*16
   elif iteration==2:
    memory=bytes(range(16))
    if ins.mnemonic=='pcmpeqw':
     mixed=bytearray(memory)
     for lane in range(1,8,2):mixed[lane*2]^=1
     memory=bytes(mixed)
   if ins.mnemonic=='cvtdq2pd':memory=pair+memory[8:]
   if ins.mnemonic=='ucomisd':memory=struct.pack('<Q',right_value)+memory[8:]
   if ins.mnemonic=='cvttss2si':memory=struct.pack('<I',source_value)+memory[4:]
   if ins.mnemonic=='cvtpd2ps':memory=packed_source
   if ins.mnemonic in {'addsd','subsd','mulss'}:memory=scalar_memory+memory[len(scalar_memory):]
   u.mem_write(address,memory)
  for register,value in zip(regs,general):u.reg_write(register,value)
  u.reg_write(X.UC_X86_REG_EFLAGS,flags);u.reg_write(X.UC_X86_REG_MXCSR,mxcsr)
  for i in range(8):u.reg_write(getattr(X,'UC_X86_REG_XMM'+str(i)),int.from_bytes(initial[i*16:i*16+16],'little'))
  code=bytes(ins.bytes);u.mem_write(0x1000,code);u.emu_start(0x1000,0x1000+len(code))
  result=[u.reg_read(register) for register in regs]+[u.reg_read(X.UC_X86_REG_EFLAGS)];expected=b''.join(u.reg_read(getattr(X,'UC_X86_REG_XMM'+str(i))).to_bytes(16,'little') for i in range(8));after=bytes(u.mem_read(address,16)) if size else memory
  assert u.reg_read(X.UC_X86_REG_MXCSR)==mxcsr
  if ins.mnemonic=='ucomisd':
   left_raw=initial[left_index*16:left_index*16+16]
   right_raw=initial[right_index*16:right_index*16+16] if ins.operands[1].type==C.X86_OP_REG else memory
   a0,a1=struct.unpack('<2Q',left_raw);b0,b1=struct.unpack('<2Q',right_raw)
   compare_indices.append(len(cases));compare_requests.append(f'5 {mxcsr:x} {flags&0x8d5:x} {a0:016x} {a1:016x} {b0:016x} {b1:016x}\n')
  if ins.mnemonic=='cvttss2si':
   left_raw=initial[:16]
   right_raw=initial[source_index*16:source_index*16+16] if ins.operands[1].type==C.X86_OP_REG else memory
   a0,a1=struct.unpack('<2Q',left_raw);b0,b1=struct.unpack('<2Q',right_raw)
   compare_indices.append(len(cases));compare_requests.append(f'4 {mxcsr:x} {flags&0x8d5:x} {a0:016x} {a1:016x} {b0:016x} {b1:016x}\n')
  if ins.mnemonic=='cvtpd2ps':
   left_index=int(ins.reg_name(ins.operands[0].reg)[3:]);left_raw=initial[left_index*16:left_index*16+16]
   right_raw=initial[source_index*16:source_index*16+16] if ins.operands[1].type==C.X86_OP_REG else memory
   a0,a1=struct.unpack('<2Q',left_raw);b0,b1=struct.unpack('<2Q',right_raw)
   compare_indices.append(len(cases));compare_requests.append(f'3 {mxcsr:x} {flags&0x8d5:x} {a0:016x} {a1:016x} {b0:016x} {b1:016x}\n')
  if ins.mnemonic in {'addsd','subsd','mulss'}:
   left_raw=initial[left_index*16:left_index*16+16];right_raw=initial[source_index*16:source_index*16+16] if ins.operands[1].type==C.X86_OP_REG else memory
   a0,a1=struct.unpack('<2Q',left_raw);b0,b1=struct.unpack('<2Q',right_raw);op={'addsd':0,'subsd':1,'mulss':2}[ins.mnemonic]
   compare_indices.append(len(cases));compare_requests.append(f'{op} {mxcsr:x} {flags&0x8d5:x} {a0:016x} {a1:016x} {b0:016x} {b1:016x}\n')
  cases.append((index,address,size,mxcsr,mxcsr,general+[flags],result,initial,expected,memory,after))
compare_oracle=None
if a.compare or a.truncate or a.pd2ps or a.scalar:
 oracle_source=R/'tests/sse_fp_oracle.c';oracle_binary=out/'compare-oracle-x86'
 subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(oracle_source),'-o',str(oracle_binary)],check=True)
 request=''.join(compare_requests);(out/'compare-requests.txt').write_text(request)
 run=subprocess.run(['arch','-x86_64',str(oracle_binary)],input=request,text=True,capture_output=True,check=True,timeout=60);(out/'compare-results.txt').write_text(run.stdout)
 replies=run.stdout.splitlines();assert len(replies)==len(compare_indices)
 for case_index,reply in zip(compare_indices,replies):
  a0,a1,integer,after_mxcsr,after_flags=[int(v,16) for v in reply.split()]
  row=list(cases[case_index])
  site=sites[row[0]];ins=next(decoder.disasm(bytes.fromhex(site['bytes_hex']),0));left_index=int(ins.reg_name(ins.operands[0].reg)[3:]) if ins.mnemonic in {'ucomisd','cvtpd2ps','addsd','subsd','mulss'} else 0
  if ins.mnemonic in {'cvtpd2ps','addsd','subsd','mulss'}:
   vector=bytearray(row[7]);vector[left_index*16:left_index*16+16]=struct.pack('<2Q',a0,a1);row[8]=bytes(vector)
  else:
   assert row[7]==row[8] and (a0,a1)==struct.unpack('<2Q',row[7][left_index*16:left_index*16+16])
  if ins.mnemonic=='cvttss2si':
   destination_index=regindex[ins.reg_name(ins.operands[0].reg)];assert row[6][destination_index]==integer;row[6][destination_index]=integer
  else:assert integer==0
  assert after_flags==row[6][-1]&0x8d5
  row[4]=after_mxcsr;row[6][-1]=(row[6][-1]&~0x8d5)|after_flags;cases[case_index]=tuple(row)
 compare_oracle={'kind':'Offline legacy scalar arithmetic/comparison/conversion through Rosetta; its floating vector results and MXCSR supply expectations','cases':len(compare_indices),'source_sha256':sha(oracle_source),'binary_sha256':sha(oracle_binary),'request_sha256':sha(out/'compare-requests.txt'),'results_sha256':sha(out/'compare-results.txt'),'limits':['Masked exceptions only','Not physical Intel hardware proof','Memory operands use scalar values in the reference; actual memory addressing is tested by emitted native bodies']}

def array(values):return '{'+','.join(str(v)+'u' for v in values)+'}'
table=out/'cases.inc';table.write_text('\n'.join(f'extern void test_{i}(_cpu *);' for i in range(len(sites)))+'\nstatic void (*const functions[])(_cpu *)={'+','.join(f'test_{i}' for i in range(len(sites)))+'};\nstatic const Case cases[]={\n'+',\n'.join('{'+','.join([str(v)+'u' for v in row[:5]]+[array(v) for v in row[5:]])+'}' for row in cases)+'\n};\n')
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();sources=[ir_path,R/'port/llasm-support/tribes_sse.c',R/'tests/sse_lowering_test.c'];runs=[]
math=sse_inputs('simulator' if a.simulator_device else 'macos')
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 run=subprocess.run(cmd+math['compile_flags']+list(map(str,sources))+math['sources']+math['libraries']+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':sha(exe)})
paths=[R/'scripts/patches/srw-tribes.patch',R/'port/llasm-support/tribes_sse.c',R/'port/llasm-support/tribes_sse.h',R/'port/llasm-support/tribes_sse.llinc',R/'port/llasm-support/llasm_cpu.h',R/'tests/sse_lowering_test.c',pathlib.Path(__file__).resolve(),R/'tests/sse_fp_oracle.c']
report={'state':'finished','pass':True,'sse_math':math['evidence'],'target':target,'simulator_device':a.simulator_device,'original_image_sha256':sha(original),'requirements_sha256':sha(R/'generated/qol-simd-requirements.json'),'srw_sha256':sha(srw),'llasm_sha256':sha(R/'generated/tools/llasm/llasm'),'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'scalar_enabled':a.scalar,'pd2ps_enabled':a.pd2ps,'truncate_enabled':a.truncate,'compare_enabled':a.compare,'compare_oracle':compare_oracle,'packed_enabled':a.packed,'dq2pd_enabled':a.dq2pd,'families':sorted(names),'original_sites':original_sites,'emitted_instruction_blocks':len(sites),'cases':len(cases),'sites':sites,'negative':negative,'fixture_sha256':sha(out/'fixture/fixture.exe'),'corpus_sha256':sha(table),'emitted_llasm_sha256':sha(llasm),'llvm_sha256':sha(ir_path),'runs':runs,'scope':'Actual SRW-emitted legacy SIMD bodies from isolated original-byte/synthetic operand fixtures, compiled ahead of time to native ARM64. Full registers/flags/XMM/MXCSR/x87/stack and memory checks on independent threads. No complete QoL lift/initializer, VEX, patch publication, rendering or game-session claim'};report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='sites'},indent=2))
