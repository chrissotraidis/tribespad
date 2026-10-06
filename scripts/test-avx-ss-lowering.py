#!/usr/bin/env python3
"""Execute strict SRW VMOVSS bodies from unchanged PE instruction bytes."""
import argparse,hashlib,json,pathlib,random,re,struct,subprocess
import capstone,pefile
from capstone import x86_const as C
from sse_build_inputs import sse_inputs
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');ap.add_argument('--scalar',action='store_true');ap.add_argument('--widen',action='store_true');ap.add_argument('--xor',action='store_true');ap.add_argument('--compare',action='store_true');ap.add_argument('--sqrt',action='store_true');ap.add_argument('--narrow',action='store_true');ap.add_argument('--comiss',action='store_true');ap.add_argument('--xorps',action='store_true');ap.add_argument('--divide',action='store_true');ap.add_argument('--aps',action='store_true');ap.add_argument('--subtract',action='store_true');ap.add_argument('--aps256',action='store_true');ap.add_argument('--ups',action='store_true');ap.add_argument('--zero',action='store_true');ap.add_argument('--dq',action='store_true');ap.add_argument('--shuffle',action='store_true');ap.add_argument('--extract',action='store_true');ap.add_argument('--half',action='store_true');ap.add_argument('--packed',action='store_true');ap.add_argument('--packed-add',action='store_true');ap.add_argument('--packed-divide',action='store_true');a=ap.parse_args();a.packed_add=a.packed_add or a.packed_divide;a.packed=a.packed or a.packed_add;a.half=a.half or a.packed;a.extract=a.extract or a.half;a.shuffle=a.shuffle or a.extract;a.dq=a.dq or a.shuffle;a.zero=a.zero or a.dq;a.ups=a.ups or a.zero;a.aps256=a.aps256 or a.ups;a.subtract=a.subtract or a.aps256;a.aps=a.aps or a.subtract;a.divide=a.divide or a.aps;a.xorps=a.xorps or a.divide;a.comiss=a.comiss or a.xorps;a.narrow=a.narrow or a.comiss;a.sqrt=a.sqrt or a.narrow;a.compare=a.compare or a.sqrt;a.xor=a.xor or a.compare;a.widen=a.widen or a.xor;a.scalar=a.scalar or a.widen
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True))
 assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
out=R/('generated/sse/vex-pdiv-lowering' if a.packed_divide else 'generated/sse/vex-add-lowering' if a.packed_add else 'generated/sse/vex-packed-lowering' if a.packed else 'generated/sse/vex-half-lowering' if a.half else 'generated/sse/vex-extract-lowering' if a.extract else 'generated/sse/vex-shuf-lowering' if a.shuffle else 'generated/sse/vex-dq-lowering' if a.dq else 'generated/sse/vex-zero-lowering' if a.zero else 'generated/sse/vex-ups-lowering' if a.ups else 'generated/sse/vex-aps256-lowering' if a.aps256 else 'generated/sse/vex-sub-lowering' if a.subtract else 'generated/sse/vex-aps-lowering' if a.aps else 'generated/sse/vex-div-lowering' if a.divide else 'generated/sse/vex-xorps-lowering' if a.xorps else 'generated/sse/vex-comiss-lowering' if a.comiss else 'generated/sse/vex-narrow-lowering' if a.narrow else 'generated/sse/vex-sqrt-lowering' if a.sqrt else 'generated/sse/vex-compare-lowering' if a.compare else 'generated/sse/vex-xor-lowering' if a.xor else 'generated/sse/vex-widen-lowering' if a.widen else 'generated/sse/vex-scalar-lowering' if a.scalar else 'generated/sse/vmovss-lowering')/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True)
report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
lock=json.loads((R/'toolchains.lock.json').read_text());build=json.loads((R/'generated/tools/SRW-tribes/tribes-build.json').read_text())
srw=R/'generated/tools/SRW-tribes/SRW.exe';original=R/'ref/product/IFC22.dll';llasm=R/'generated/tools/llasm/llasm'
assert sha(srw)==build['srw_sha256'] and build['patch_sha256']==lock['tribes_extensions']['patch_sha256']==sha(R/'scripts/patches/srw-tribes.patch')
assert sha(original)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
assert sha(llasm)==lock['artifacts']['llasm/llasm']
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
pe=pefile.PE(str(original));sites=[]
for pc,raw in [(0x1000c70b,'c5fa104604'),(0x1000c710,'c5fa100e')]:
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,len(bytes.fromhex(raw))).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL'})
def prefix(form,merge,pp=2):
 v=((15-merge)<<3)|pp
 return bytes([0xc5,0x80|v]) if form==0 else bytes([0xc4,0xe1,v|(0x80 if form==2 else 0)])
for form in range(3):
 for opcode in (0x10,0x11):
  for dest in range(8):
   for merge in range(8):
    for source in range(8):
     modrm=0xc0|((dest if opcode==0x10 else source)<<3)|(source if opcode==0x10 else dest)
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge)+bytes([opcode,modrm])).hex(),'origin':'synthetic register/alias'})
  for index in range(8):
   for addressing in [bytes([index<<3]),bytes([0x44|(index<<3),0xb3,0xef]),bytes([0x44|(index<<3),0x24,0x23]),bytes([5|(index<<3)])+struct.pack('<I',0x200003)]:
    sites.append({'pc':None,'bytes_hex':(prefix(form,0)+bytes([opcode])+addressing).hex(),'origin':'synthetic scalar memory'})
if a.scalar:
 for pc,raw in [(0x1000c714,'c5f259c9'),(0x1000c718,'c5fa59c0'),(0x1000c71c,'c5f258c0')]:
  assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,len(bytes.fromhex(raw))).hex()==raw
  sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL scalar arithmetic'})
 for form in range(3):
  for opcode in (0x59,0x58):
   for dest in range(8):
    for merge in range(8):
     for src in range(8):
      sites.append({'pc':None,'bytes_hex':(prefix(form,merge)+bytes([opcode,0xc0|(dest<<3)|src])).hex(),'origin':'synthetic scalar arithmetic alias'})
     for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
      sites.append({'pc':None,'bytes_hex':(prefix(form,merge)+bytes([opcode])+addressing).hex(),'origin':'synthetic scalar arithmetic memory'})
if a.scalar:
 for raw in ['c5f259c2','c5f258c2']:
  sites.append({'pc':None,'bytes_hex':raw,'origin':'arithmetic Cartesian reference'})
if a.widen:
 raw='c5fa5ac0';pc=0x1000c720
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL widening'})
 sites.append({'pc':None,'bytes_hex':'c5f25ac2','origin':'widening edge matrix'})
 for form in range(3):
  for dest in range(8):
   for merge in range(8):
    for src in range(8):
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge)+bytes([0x5a,0xc0|(dest<<3)|src])).hex(),'origin':'synthetic widening alias'})
    for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge)+bytes([0x5a])+addressing).hex(),'origin':'synthetic widening memory'})
if a.xor:
 raw='c5f157c9';pc=0x1000c724
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL bitwise'})
 for form in range(3):
  for dest in range(8):
   for merge in range(8):
    for src in range(8):
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge,1)+bytes([0x57,0xc0|(dest<<3)|src])).hex(),'origin':'synthetic bitwise alias'})
    for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge,1)+bytes([0x57])+addressing).hex(),'origin':'synthetic bitwise memory'})
if a.compare:
 raw='c5f92ec8';pc=0x1000c728
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL comparison'})
 sites.append({'pc':None,'bytes_hex':'c5f92eca','origin':'comparison edge matrix'})
 for form in range(3):
  for left in range(8):
   for right in range(8):
    sites.append({'pc':None,'bytes_hex':(prefix(form,0,1)+bytes([0x2e,0xc0|(left<<3)|right])).hex(),'origin':'synthetic comparison alias'})
   for addressing in [bytes([left<<3]),bytes([0x44|(left<<3),0xb3,0xef]),bytes([0x44|(left<<3),0x24,0x23]),bytes([5|(left<<3)])+struct.pack('<I',0x200003)]:
    sites.append({'pc':None,'bytes_hex':(prefix(form,0,1)+bytes([0x2e])+addressing).hex(),'origin':'synthetic comparison memory'})
if a.sqrt:
 raw='c5fb51c0';pc=0x1000c72e
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL square root'})
 sites.append({'pc':None,'bytes_hex':'c5f351c2','origin':'square root edge matrix'})
 for form in range(3):
  for dest in range(8):
   for merge in range(8):
    for src in range(8):
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge,3)+bytes([0x51,0xc0|(dest<<3)|src])).hex(),'origin':'synthetic square root alias'})
    for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge,3)+bytes([0x51])+addressing).hex(),'origin':'synthetic square root memory'})
if a.narrow:
 raw='c5fb5ac8';pc=0x1000c739
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL narrowing'})
 sites.append({'pc':None,'bytes_hex':'c5f35ac2','origin':'narrowing edge matrix'})
 for form in range(3):
  for dest in range(8):
   for merge in range(8):
    for src in range(8):
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge,3)+bytes([0x5a,0xc0|(dest<<3)|src])).hex(),'origin':'synthetic narrowing alias'})
    for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge,3)+bytes([0x5a])+addressing).hex(),'origin':'synthetic narrowing memory'})
if a.comiss:
 raw='c5f82fc1';pc=0x1000c745
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL ordered comparison'})
 sites.append({'pc':None,'bytes_hex':'c5f82fca','origin':'ordered comparison edge matrix'})
 for form in range(3):
  for left in range(8):
   for right in range(8):
    sites.append({'pc':None,'bytes_hex':(prefix(form,0,0)+bytes([0x2f,0xc0|(left<<3)|right])).hex(),'origin':'synthetic ordered comparison alias'})
   for addressing in [bytes([left<<3]),bytes([0x44|(left<<3),0xb3,0xef]),bytes([0x44|(left<<3),0x24,0x23]),bytes([5|(left<<3)])+struct.pack('<I',0x200003)]:
    sites.append({'pc':None,'bytes_hex':(prefix(form,0,0)+bytes([0x2f])+addressing).hex(),'origin':'synthetic ordered comparison memory'})
if a.xorps:
 raw='c5f857c0';pc=0x1000c74b
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL single packed bitwise'})
 for form in range(3):
  for dest in range(8):
   for merge in range(8):
    for src in range(8):
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge,0)+bytes([0x57,0xc0|(dest<<3)|src])).hex(),'origin':'synthetic single packed bitwise alias'})
    for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge,0)+bytes([0x57])+addressing).hex(),'origin':'synthetic single packed bitwise memory'})
if a.divide:
 raw='c5fa5ec9';pc=0x1000c765
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL division'})
 sites.append({'pc':None,'bytes_hex':'c5f25ec2','origin':'division Cartesian reference'})
 for form in range(3):
  for dest in range(8):
   for merge in range(8):
    for src in range(8):
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge)+bytes([0x5e,0xc0|(dest<<3)|src])).hex(),'origin':'synthetic division alias'})
    for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge)+bytes([0x5e])+addressing).hex(),'origin':'synthetic division memory'})
if a.aps:
 raw='c5f828d0';pc=0x1000c7b6
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL aligned packed move'})
 for form in range(3):
  for opcode in [0x28,0x29]:
   for dest in range(8):
    for src in range(8):
     modrm=0xc0|((dest if opcode==0x28 else src)<<3)|(src if opcode==0x28 else dest)
     sites.append({'pc':None,'bytes_hex':(prefix(form,0,0)+bytes([opcode,modrm])).hex(),'origin':'synthetic aligned packed move alias'})
    for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200030)]:
     sites.append({'pc':None,'bytes_hex':(prefix(form,0,0)+bytes([opcode])+addressing).hex(),'origin':'synthetic aligned packed move memory'})
if a.subtract:
 raw='c5fa5c5104';pc=0x1000c823
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,5).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL subtraction'})
 sites.append({'pc':None,'bytes_hex':'c5f25cc2','origin':'subtraction Cartesian reference'})
 for form in range(3):
  for dest in range(8):
   for merge in range(8):
    for src in range(8):
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge)+bytes([0x5c,0xc0|(dest<<3)|src])).hex(),'origin':'synthetic subtraction alias'})
    for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
     sites.append({'pc':None,'bytes_hex':(prefix(form,merge)+bytes([0x5c])+addressing).hex(),'origin':'synthetic subtraction memory'})
if a.aps256:
 raw='c5fc2805e0b11c10';pc=0x1000c943
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,8).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL 256-bit aligned move'})
 for form in range(3):
  for opcode in [0x28,0x29]:
   for dest in range(8):
    for src in range(8):
     modrm=0xc0|((dest if opcode==0x28 else src)<<3)|(src if opcode==0x28 else dest)
     raw=bytearray(prefix(form,0,0));raw[-1]|=4
     sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode,modrm])).hex(),'origin':'synthetic 256-bit aligned move alias'})
    for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200020)]:
     raw=bytearray(prefix(form,0,0));raw[-1]|=4
     sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode])+addressing).hex(),'origin':'synthetic 256-bit aligned move memory'})
if a.ups:
 raw='c5fc1100';pc=0x1000c957
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL unaligned packed move'})
 for wide in [0,1]:
  for form in range(3):
   for opcode in [0x10,0x11]:
    for dest in range(8):
     for src in range(8):
      modrm=0xc0|((dest if opcode==0x10 else src)<<3)|(src if opcode==0x10 else dest)
      raw=bytearray(prefix(form,0,0));raw[-1]|=wide*4
      sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode,modrm])).hex(),'origin':'synthetic unaligned packed move alias'})
     for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
      raw=bytearray(prefix(form,0,0));raw[-1]|=wide*4
      sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode])+addressing).hex(),'origin':'synthetic unaligned packed move memory'})
if a.zero:
 raw='c5f877';pc=0x1000c960
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,3).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL zero upper'})
 for raw in ['c5f877','c4e17877','c4e1f877','64c5f877']:
  sites.append({'pc':None,'bytes_hex':raw,'origin':'synthetic zero upper'})
if a.dq:
 raw='c5f96f05a0b11c10';pc=0x1000c97c
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,8).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL packed integer move'})
 for raw in ['c5f96f06','c5f97f16','c5fd6f06','c5fd7f16']:
  sites.append({'pc':None,'bytes_hex':raw,'origin':'packed integer alignment fault probe'})
 for aligned in [True,False]:
  for wide in [0,1]:
   for form in range(3):
    for opcode in [0x6f,0x7f]:
     for dest in range(8):
      for src in range(8):
       modrm=0xc0|((dest if opcode==0x6f else src)<<3)|(src if opcode==0x6f else dest)
       raw=bytearray(prefix(form,0,1 if aligned else 2));raw[-1]|=wide*4
       sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode,modrm])).hex(),'origin':'synthetic packed integer move alias'})
      absolute=0x200020 if aligned else 0x200003
      for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',absolute)]:
       raw=bytearray(prefix(form,0,1 if aligned else 2));raw[-1]|=wide*4
       sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode])+addressing).hex(),'origin':'synthetic packed integer move memory'})
if a.shuffle:
 raw='c5f8c6c099';pc=0x1000c984
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,5).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL packed shuffle'})
 for wide in [0,1]:
  for control in range(256):
   for modrm in [0xc2,0x06]:
    raw=bytearray(prefix(0,1,0));raw[-1]|=wide*4
    sites.append({'pc':None,'bytes_hex':(raw+bytes([0xc6,modrm,control])).hex(),'origin':'shuffle complete control matrix'})
  for form in range(3):
   for control in [0,0x55,0xaa,0xff,0x99]:
    for dest in range(8):
     for left in range(8):
      for right in range(8):
       raw=bytearray(prefix(form,left,0));raw[-1]|=wide*4
       sites.append({'pc':None,'bytes_hex':(raw+bytes([0xc6,0xc0|(dest<<3)|right,control])).hex(),'origin':'synthetic shuffle alias'})
      for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
       raw=bytearray(prefix(form,left,0));raw[-1]|=wide*4
       sites.append({'pc':None,'bytes_hex':(raw+bytes([0xc6])+addressing+bytes([control])).hex(),'origin':'synthetic shuffle memory'})
if a.extract:
 pc=0x1000c998;raw='c4e37d19cb01'
 assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,6).hex()==raw
 sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL half extraction'})
 for control in range(256):
  for addressing in [bytes([0xd0]),bytes([0x16])]:
   sites.append({'pc':None,'bytes_hex':(bytes.fromhex('c4e37d19')+addressing+bytes([control])).hex(),'origin':'extraction complete control matrix'})
 for control in [0,1,2,3,0x7e,0x7f,0xfe,0xff]:
  for source in range(8):
   for dest in range(8):
    sites.append({'pc':None,'bytes_hex':(bytes.fromhex('c4e37d19')+bytes([0xc0|(source<<3)|dest,control])).hex(),'origin':'synthetic extraction alias'})
   for addressing in [bytes([source<<3]),bytes([0x44|(source<<3),0xb3,0xef]),bytes([0x44|(source<<3),0x24,0x23]),bytes([5|(source<<3)])+struct.pack('<I',0x200003)]:
    sites.append({'pc':None,'bytes_hex':(bytes.fromhex('c4e37d19')+addressing+bytes([control])).hex(),'origin':'synthetic extraction memory'})
if a.half:
 for pc,raw in [(0x1000c9a4,'c5f812ca'),(0x1000c9a8,'c5e816c0')]:
  assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
  sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL half merging'})
 for form in range(3):
  for opcode in [0x12,0x16]:
   for dest in range(8):
    for merge in range(8):
     for src in range(8):
      sites.append({'pc':None,'bytes_hex':(prefix(form,merge,0)+bytes([opcode,0xc0|(dest<<3)|src])).hex(),'origin':'synthetic half merge alias'})
     for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
      sites.append({'pc':None,'bytes_hex':(prefix(form,merge,0)+bytes([opcode])+addressing).hex(),'origin':'synthetic half load'})
  for opcode in [0x13,0x17]:
   for src in range(8):
    for addressing in [bytes([src<<3]),bytes([0x44|(src<<3),0xb3,0xef]),bytes([0x44|(src<<3),0x24,0x23]),bytes([5|(src<<3)])+struct.pack('<I',0x200003)]:
     sites.append({'pc':None,'bytes_hex':(prefix(form,0,0)+bytes([opcode])+addressing).hex(),'origin':'synthetic half store'})
if a.packed:
 for pc,raw in [(0x1000c9e9,'c5c059c2'),(0x1000c9f1,'c5f05cc0')]:
  assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
  sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL packed arithmetic'})
 for wide in range(2):
  for opcode in [0x59,0x5c]:
   for modrm in [0xc2,0x06]:
    raw=bytearray(prefix(0,1,0));raw[-1]|=wide*4
    sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode,modrm])).hex(),'origin':'packed Cartesian reference'})
   for form in range(3):
    for dest in range(8):
     for left in range(8):
      raw=bytearray(prefix(form,left,0));raw[-1]|=wide*4
      for right in range(8):
       sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode,0xc0|(dest<<3)|right])).hex(),'origin':'synthetic packed arithmetic alias'})
      for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
       sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode])+addressing).hex(),'origin':'synthetic packed arithmetic memory'})
if a.packed_add:
 for pc,raw in [(0x1000ca9d,'c5f058c0')]:
  assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
  sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL packed arithmetic'})
 for wide in range(2):
  for opcode in [0x58]:
   for modrm in [0xc2,0x06]:
    raw=bytearray(prefix(0,1,0));raw[-1]|=wide*4
    sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode,modrm])).hex(),'origin':'packed Cartesian reference'})
   for form in range(3):
    for dest in range(8):
     for left in range(8):
      raw=bytearray(prefix(form,left,0));raw[-1]|=wide*4
      for right in range(8):
       sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode,0xc0|(dest<<3)|right])).hex(),'origin':'synthetic packed arithmetic alias'})
      for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
       sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode])+addressing).hex(),'origin':'synthetic packed arithmetic memory'})
if a.packed_divide:
 for pc,raw in [(0x1000cb44,'c5f85ec9')]:
  assert pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,4).hex()==raw
  sites.append({'pc':pc,'bytes_hex':raw,'origin':'original QoL packed arithmetic'})
 for wide in range(2):
  for opcode in [0x5e]:
   for modrm in [0xc2,0x06]:
    raw=bytearray(prefix(0,1,0));raw[-1]|=wide*4
    sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode,modrm])).hex(),'origin':'packed Cartesian reference'})
   for form in range(3):
    for dest in range(8):
     for left in range(8):
      raw=bytearray(prefix(form,left,0));raw[-1]|=wide*4
      for right in range(8):
       sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode,0xc0|(dest<<3)|right])).hex(),'origin':'synthetic packed arithmetic alias'})
      for addressing in [bytes([dest<<3]),bytes([0x44|(dest<<3),0xb3,0xef]),bytes([0x44|(dest<<3),0x24,0x23]),bytes([5|(dest<<3)])+struct.pack('<I',0x200003)]:
       sites.append({'pc':None,'bytes_hex':(raw+bytes([opcode])+addressing).hex(),'origin':'synthetic packed arithmetic memory'})
for site in sites:
 ins=next(decoder.disasm(bytes.fromhex(site['bytes_hex']),0));assert ins.mnemonic in {'vmulps','vsubps','vaddps','vdivps','vmovhlps','vmovlhps','vmovlps','vmovhps','vextractf128','vshufps','vmovdqa','vmovdqu','vzeroupper','vmovss','vmovups','vmovaps','vmulss','vdivss','vsubss','vaddss','vcvtss2sd','vxorpd','vxorps','vucomisd','vsqrtsd','vcvtsd2ss','vcomiss'} and ins.size==len(bytes.fromhex(site['bytes_hex']))
 site['assembly']=ins.mnemonic+' '+ins.op_str
# Bound code ranges so original absolute data addresses never become fake text.
fixture_batches=[];bodies={}
absolute_addresses=[]
for site in sites:
 ins=next(decoder.disasm(bytes.fromhex(site['bytes_hex']),0))
 absolute_addresses.extend(op.mem.disp&0xffffffff for op in ins.operands if op.type==C.X86_OP_MEM and not op.mem.base and not op.mem.index)
for start in range(0,len(sites),40000):
 chunk=sites[start:start+40000];directory=out/('fixture' if not start else 'fixture-'+str(start))
 code_start=0x10001000;code_end=code_start+32*len(chunk)
 assert all(not code_start<=address<code_end for address in absolute_addresses)
 run=fixture(directory,chunk,original,srw);assert not run.returncode,run.stderr
 text=(directory/'seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(chunk)
 fixture_batches.append({'first':start,'count':len(chunk),'code_start':hex(code_start),'code_end':hex(code_end),'pe_sha256':sha(directory/'fixture.exe'),'emitted_sha256':sha(directory/'seg01_code.llinc')})
 for match in procedures:
  parts=match[2].split(';ret\n');assert len(parts)==2
  body=parts[0].strip();assert body.startswith((';vmulps ',';vsubps ',';vaddps ',';vdivps ',';vmovhlps ',';vmovlhps ',';vmovlps ',';vmovhps ',';vextractf128 ',';vshufps ',';vmovdqa ',';vmovdqu ',';vzeroupper',';vmovss ',';vmovups ',';vmovaps ',';vmulss ',';vdivss ',';vsubss ',';vaddss ',';vcvtss2sd ',';vxorpd ',';vxorps ',';vucomisd ',';vsqrtsd ',';vcvtsd2ss ',';vcomiss ')) and ('call tb_avx_' in body or 'call tb_sse_ucomisd' in body)
  bodies[start+int(match[1])]=body
assert len(bodies)==len(sites)
source=out/'blocks.llasm';source.write_text('include tribes_sse.llinc\n'+''.join(f'proc test_{i} public\n{bodies[i]}\nendp\n' for i in range(len(sites))))
ir_path=out/'blocks.ll';run=subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-o',str(ir_path),str(source)],capture_output=True,text=True)
(out/'llasm.log').write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
ir=ir_path.read_text();assert ir.count('define hidden fastcc void @test_')==len(sites);ir_path.write_text(ir.replace('define hidden fastcc void @test_','define ccc void @test_'))
negative={}
for name,raw in [('packed-divpd','c5f15ec2'),('packed-divsd','c5f35ec2'),('packed-fs-div','64c5f05e06'),('packed-addpd','c5f158c2'),('packed-addsd','c5f358c2'),('packed-fs-add','64c5f05806'),('packed-mulpd','c5f159c2'),('packed-subpd','c5f15cc2'),('packed-mulsd','c5f359c2'),('packed-map','c4e37059c2'),('packed-fs-mul','64c5f05906'),('packed-fs-sub','64c5f05c06'),('packed-address16','67c5f05900'),('half-l1-register','c5f412c2'),('half-l1-load','c5f41606'),('half-l1-store','c5fc1706'),('half-66','c5f112c2'),('half-f3','c5f216c2'),('half-f2','c5f316c2'),('half-store-register-low','c5f813c2'),('half-store-register-high','c5f817c2'),('half-store-vvvv-low','c5f01306'),('half-store-vvvv-high','c5f01706'),('half-fs-load','64c5f01206'),('half-fs-store','64c5f81706'),('half-address16','67c5f01200'),('half-map','c4e37012c2'),('vextract128-l0','c4e37919d001'),('vextract128-w1','c4e3fd19d001'),('vextract128-vvvv','c4e37519d001'),('vextract128-pp0','c4e37c19d001'),('vextract128-f3','c4e37e19d001'),('vextract128-f2','c4e37f19d001'),('vextract128-map0f','c4e17d19d001'),('vextract128-map0f38','c4e27d19d001'),('vextract128-extended','c4c37d19d001'),('vextract128-address16','67c4e37d190001'),('vextract128-fs-memory','64c4e37d191601'),('vextract128-lock','f0c4e37d19d001'),('vextract128-legacy66','66c4e37d19d001'),('fs-third-memory-mulss','64c5f25906'),('fs-third-memory-addss','64c5f25806'),('fs-third-memory-subss','64c5f25c06'),('fs-third-memory-divss','64c5f25e06'),('fs-third-memory-ss2sd','64c5f25a06'),('fs-third-memory-sd2ss','64c5f35a06'),('fs-third-memory-sqrtsd','64c5f35106'),('fs-third-memory-xorpd','64c5f15706'),('fs-third-memory-xorps','64c5f05706'),('vshufpd-unsupported','c5f1c6c299'),('vshufps-f3','c5f2c6c299'),('vshufps-f2','c5f3c6c299'),('vshufps-map','c4e370c6c299'),('vshufps-address16','67c5f0c60099'),('vshufps-fs-memory','64c5f0c60699'),('vmovdq-no-prefix','c5f86fc2'),('vmovdq-f2','c5fb6fc2'),('vmovdqa-reserved-register','c5f16fc2'),('vmovdqa-reserved-load','c5f16f06'),('vmovdqa-reserved-store','c5f17f16'),('vmovdqa256-reserved','c5f56fc2'),('vmovdqu-reserved-register','c5f26fc2'),('vmovdqu-reserved-load','c5f26f06'),('vmovdqu-reserved-store','c5f27f16'),('vmovdqu256-reserved','c5f66fc2'),('vzeroall-unsupported','c5fc77'),('vzeroupper-vvvv','c5f077'),('vzeroupper-pp','c5f977'),('vzeroupper-legacy66','66c5f877'),('vzeroupper-legacyf3','f3c5f877'),('vzeroupper-lock','f0c5f877'),('vzeroupper-address16','67c5f877'),('vmovups-reserved-register','c5f010c2'),('vmovups-reserved-load','c5f01006'),('vmovups-reserved-store','c5f01106'),('vmovups256-reserved-register','c5f410c2'),('vmovups256-reserved-load','c5f41006'),('vmovups256-reserved-store','c5f41106'),('vmovupd-unsupported','c5f910c2'),('vmovupd256-unsupported','c5fd10c2'),('vmovaps-reserved-register','c5f028c2'),('vmovaps-reserved-load','c5f02806'),('vmovaps-reserved-store','c5f02906'),('vmovaps256-reserved-register','c5f428c2'),('vmovaps256-reserved-load','c5f42806'),('vmovaps256-reserved-store','c5f42906'),('vmovapd256-unsupported','c5fd28c2'),('vmovapd-unsupported','c5f928c2'),('vdivss-l1','c5f65ec2'),('vdivsd-unsupported','c5f35ec2'),('vcomiss-reserved-register','c5f02fca'),('vcomiss-reserved-memory','c5f02f0e'),('vcomiss-l1','c5fc2fca'),('vcomisd-unsupported','c5f92fca'),('vucomiss-unsupported','c5f82eca'),('vcvtsd2ss-l1','c5f75ac2'),('conversion-66','c5f15ac2'),('vsqrtsd-l1','c5f751c2'),('vsqrtss-unsupported','c5f251c2'),('vsqrtsd-66','c5f151c2'),('vucomisd-reserved-register','c5f12eca'),('vucomisd-reserved-memory','c5f12e0e'),('vucomisd-256','c5fd2eca'),('vxorpd-256','c5f557c1'),('vxorps-256','c5f457c1'),('vandps-unsupported','c5f054c1'),('vxorpd-f3','c5f257c1'),('vmovsd-reserved-load','c5f31006'),('vsubsd-unsupported','c5f35cc9'),('vsubss-l1','c5f65cc9'),('l1','c5fe10c1'),('reserved-vvvv-load','c5f21000'),('reserved-vvvv-store','c5f21100'),('lock','f0c5fa1000'),('legacy-f3','f3c5fa1000'),('legacy-66','66c5fa1000'),('unsupported-map','c4e27a10c1'),('address16','67c5fa1000'),('fs-memory','64c5fa1000')]:
 run=fixture(out/('negative-'+name),[{'bytes_hex':raw}],original,srw);assert run.returncode and 'Error:' in run.stderr,(name,run.stderr)
 negative[name]={'exit':run.returncode,'state':'strict rejection'}
rng=random.Random(25034);cases=[];requests=[];regnames=['eax','ecx','edx','ebx','esp','ebp','esi','edi'];register_cases=0
def vector(raw,index):return raw[index*16:index*16+16]+raw[128+index*16:144+index*16]
for index,site in enumerate(sites):
 ins=next(decoder.disasm(bytes.fromhex(site['bytes_hex']),0));ops=ins.operands
 for_iteration=range((9216 if site['origin']=='packed Cartesian reference' else 512 if site['pc'] else 64 if any(op.type==C.X86_OP_MEM for op in ops) else 32) if ins.mnemonic in {'vmulps','vsubps','vaddps','vdivps'} else (64 if any(op.type==C.X86_OP_MEM for op in ops) else 32) if ins.mnemonic in {'vmovhlps','vmovlhps','vmovlps','vmovhps','vextractf128','vshufps'} else (64 if ins.mnemonic=='vmovdqu' and any(op.type==C.X86_OP_MEM for op in ops) else 32) if ins.mnemonic in {'vmovdqa','vmovdqu'} else 64 if ins.mnemonic=='vzeroupper' else (64 if any(op.type==C.X86_OP_MEM for op in ops) else 32) if ins.mnemonic=='vmovups' else (9216 if site['origin']=='subtraction Cartesian reference' else 512 if site['pc'] else 96) if ins.mnemonic=='vsubss' else 32 if ins.mnemonic=='vmovaps' else (9216 if site['origin']=='division Cartesian reference' else 512 if site['pc'] else 96) if ins.mnemonic=='vdivss' else (6400 if site['origin']=='ordered comparison edge matrix' else 512 if site['pc'] else 352) if ins.mnemonic=='vcomiss' else (1024 if site['pc'] or site['origin']=='narrowing edge matrix' else 96) if ins.mnemonic=='vcvtsd2ss' else (512 if site['pc'] or site['origin']=='square root edge matrix' else 64) if ins.mnemonic=='vsqrtsd' else (6400 if site['origin']=='comparison edge matrix' else 512 if site['pc'] else 128) if ins.mnemonic=='vucomisd' else 32 if ins.mnemonic=='vxorps' else 8 if ins.mnemonic=='vxorpd' else (512 if site['pc'] or site['origin']=='widening edge matrix' else 64) if ins.mnemonic=='vcvtss2sd' else 6400 if site['origin']=='arithmetic Cartesian reference' else 320 if ins.mnemonic!='vmovss' and site['pc'] else 32 if ins.mnemonic!='vmovss' else 4)
 for iteration in for_iteration:
  initial=bytes(rng.getrandbits(8) for _ in range(256));memory=bytes(rng.getrandbits(8) for _ in range(64));regs=[rng.getrandbits(32) for _ in range(8)]
  if iteration==0:initial=bytes(256)
  elif iteration==1:initial=b'\xff'*256
  elif iteration==2:initial=bytes(range(256))
  flags=[0,0x8d5,0x41,0x884][iteration%4];control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(0x3f if iteration%2 and iteration>=(512 if ins.mnemonic=='vcvtss2sd' and (site['pc'] or site['origin']=='widening edge matrix') else 64 if ins.mnemonic=='vcvtss2sd' else 256 if site['pc'] else 6400 if site['origin']=='arithmetic Cartesian reference' else 16) else 0)
  immediate=0;guest_base=0x200000;offset=[1,2,3,7][iteration%4]
  if ins.mnemonic=='vxorps':
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=16 else 0)
   if iteration==3:initial=struct.pack('<I',0x7f800001)*64
   elif iteration==4:initial=struct.pack('<I',1)*64
  arithmetic=ins.mnemonic not in {'vmulps','vsubps','vaddps','vdivps','vmovhlps','vmovlhps','vmovlps','vmovhps','vextractf128','vshufps','vmovdqa','vmovdqu','vzeroupper','vmovss','vmovups','vmovaps','vxorpd','vxorps','vucomisd','vsqrtsd','vcvtsd2ss','vcomiss'}
  comparison=ins.mnemonic in {'vucomisd','vcomiss'}
  ordered_comparison=ins.mnemonic=='vcomiss'
  square_root=ins.mnemonic=='vsqrtsd'
  narrowing=ins.mnemonic=='vcvtsd2ss'
  if ins.mnemonic=='vzeroupper':
   kind=31;dest=0;merge=1;src=2;mem=None;register_cases+=1
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=16 else 0)
  elif ins.mnemonic=='vmovaps':
   wide=ops[0].size==32
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=16 else 0)
   if ops[0].type==C.X86_OP_REG and ops[1].type==C.X86_OP_REG:
    dest=int(ins.reg_name(ops[0].reg)[3:]);src=int(ins.reg_name(ops[1].reg)[3:]);merge=0;mem=None
    kind=(19 if wide else 14) if bytes.fromhex(site['bytes_hex'])[-2]==0x28 else (20 if wide else 15);register_cases+=1
   else:
    kind=(22 if wide else 17) if ops[0].type==C.X86_OP_MEM else (21 if wide else 16)
    store=kind in (17,22);mem=ops[0 if store else 1];dest=src=int(ins.reg_name(ops[1 if store else 0].reg)[3:]);merge=0
    offset=[0,32][iteration%2] if wide else [0,16,32,48][iteration%4]
  elif ins.mnemonic=='vmovups':
   wide=ops[0].size==32
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=16 else 0)
   if iteration==3:initial=struct.pack('<I',0x7f800001)*64
   elif iteration==4:initial=struct.pack('<I',1)*64
   if ops[0].type==C.X86_OP_REG and ops[1].type==C.X86_OP_REG:
    dest=int(ins.reg_name(ops[0].reg)[3:]);src=int(ins.reg_name(ops[1].reg)[3:]);merge=0;mem=None
    kind=(27 if wide else 23) if bytes.fromhex(site['bytes_hex'])[-2]==0x10 else (28 if wide else 24);register_cases+=1
   else:
    store=ops[0].type==C.X86_OP_MEM;kind=(30 if wide else 26) if store else (29 if wide else 25)
    mem=ops[0 if store else 1];dest=src=int(ins.reg_name(ops[1 if store else 0].reg)[3:]);merge=0
    offset=iteration%(33 if wide else 49)
  elif ins.mnemonic in {'vmovdqa','vmovdqu'}:
   aligned=ins.mnemonic=='vmovdqa';wide=ops[0].size==32;start=(32 if aligned else 40)+(4 if wide else 0)
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=16 else 0)
   if iteration==3:initial=struct.pack('<I',0x7f800001)*64
   elif iteration==4:initial=struct.pack('<I',1)*64
   if ops[0].type==C.X86_OP_REG and ops[1].type==C.X86_OP_REG:
    dest=int(ins.reg_name(ops[0].reg)[3:]);src=int(ins.reg_name(ops[1].reg)[3:]);merge=0;mem=None
    kind=start+(bytes.fromhex(site['bytes_hex'])[-2]==0x7f);register_cases+=1
   else:
    store=ops[0].type==C.X86_OP_MEM;kind=start+2+store
    mem=ops[0 if store else 1];dest=src=int(ins.reg_name(ops[1 if store else 0].reg)[3:]);merge=0
    offset=([0,32][iteration%2] if wide else [0,16,32,48][iteration%4]) if aligned else iteration%(33 if wide else 49)
  elif ins.mnemonic in {'vmulps','vsubps','vaddps','vdivps'}:
   wide=ops[0].size==32;dest=int(ins.reg_name(ops[0].reg)[3:]);merge=int(ins.reg_name(ops[1].reg)[3:]);mem=ops[2] if ops[2].type==C.X86_OP_MEM else None
   src=dest if mem is not None else int(ins.reg_name(ops[2].reg)[3:]);kind=(72 if ins.mnemonic=='vdivps' else 68 if ins.mnemonic=='vaddps' else 64 if ins.mnemonic=='vsubps' else 60)+wide+(2 if mem is not None else 0)
   if mem is None:register_cases+=1
   offset=iteration%(33 if wide else 49)
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=16 and site['origin']!='packed Cartesian reference' else 0)
  elif ins.mnemonic in {'vmovhlps','vmovlhps','vmovlps','vmovhps'}:
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=16 else 0)
   if iteration==3:initial=struct.pack('<I',0x7f800001)*64
   elif iteration==4:initial=struct.pack('<I',1)*64
   offset=iteration%57
   if ins.mnemonic in {'vmovhlps','vmovlhps'}:
    dest,merge,src=[int(ins.reg_name(op.reg)[3:]) for op in ops];kind=54 if ins.mnemonic=='vmovhlps' else 55;mem=None;register_cases+=1
   elif ops[0].type==C.X86_OP_MEM:
    kind=58 if ins.mnemonic=='vmovlps' else 59;mem=ops[0];dest=src=int(ins.reg_name(ops[1].reg)[3:]);merge=0
   else:
    kind=56 if ins.mnemonic=='vmovlps' else 57;dest=int(ins.reg_name(ops[0].reg)[3:]);merge=int(ins.reg_name(ops[1].reg)[3:]);src=dest;mem=ops[2]
  elif ins.mnemonic=='vextractf128':
   src=int(ins.reg_name(ops[1].reg)[3:]);merge=0
   mem=ops[0] if ops[0].type==C.X86_OP_MEM else None
   dest=src if mem is not None else int(ins.reg_name(ops[0].reg)[3:]);kind=53 if mem is not None else 52
   if mem is None:register_cases+=1
   immediate=ops[2].imm&255;offset=iteration%49
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=16 else 0)
   if iteration==3:initial=struct.pack('<I',0x7f800001)*64
   elif iteration==4:initial=struct.pack('<I',1)*64
  elif ins.mnemonic=='vshufps':
   wide=ops[0].size==32;dest=int(ins.reg_name(ops[0].reg)[3:]);merge=int(ins.reg_name(ops[1].reg)[3:])
   mem=ops[2] if ops[2].type==C.X86_OP_MEM else None
   src=int(ins.reg_name(ops[2].reg)[3:]) if mem is None else dest
   kind=(51 if wide else 50) if mem is not None else (49 if wide else 48)
   if mem is None:register_cases+=1
   immediate=ops[3].imm&255;offset=iteration%(33 if wide else 49)
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=16 else 0)
   if iteration==3:initial=struct.pack('<I',0x7f800001)*64
   elif iteration==4:initial=struct.pack('<I',1)*64
  elif comparison:
   kind=11 if ordered_comparison else 8;dest=merge=int(ins.reg_name(ops[0].reg)[3:]);src=int(ins.reg_name(ops[1].reg)[3:]) if ops[1].type==C.X86_OP_REG else dest
   mem=ops[1] if ops[1].type==C.X86_OP_MEM else None
   if mem is None:register_cases+=1
  elif arithmetic or square_root or narrowing or ins.mnemonic in {'vxorpd','vxorps'}:
   kind=10 if narrowing else 9 if square_root else 12 if ins.mnemonic=='vxorps' else 7 if ins.mnemonic=='vxorpd' else 6 if ins.mnemonic=='vcvtss2sd' else 18 if ins.mnemonic=='vsubss' else 13 if ins.mnemonic=='vdivss' else 4 if ins.mnemonic=='vmulss' else 5
   dest=int(ins.reg_name(ops[0].reg)[3:]);merge=int(ins.reg_name(ops[1].reg)[3:])
   src=int(ins.reg_name(ops[2].reg)[3:]) if ops[2].type==C.X86_OP_REG else dest
   mem=ops[2] if ops[2].type==C.X86_OP_MEM else None
   if mem is None:register_cases+=1
  elif len(ops)==3:
   dest,merge,src=[int(ins.reg_name(op.reg)[3:]) for op in ops];kind=0 if bytes.fromhex(site['bytes_hex'])[-2]==0x10 else 1;register_cases+=1;mem=None
  else:
   mem=ops[0] if ops[0].type==C.X86_OP_MEM else ops[1];kind=3 if ops[0].type==C.X86_OP_MEM else 2
   dest=src=int(ins.reg_name(ops[1 if kind==3 else 0].reg)[3:]);merge=0
  if mem is not None:
   if mem.mem.base:
    base=regnames.index(ins.reg_name(mem.mem.base));scaled=regs[regnames.index(ins.reg_name(mem.mem.index))]*mem.mem.scale if mem.mem.index else 0
    regs[base]=(0x200000+offset-scaled-mem.mem.disp)&0xffffffff
   else:
    if site['pc'] and kind in (21,34):guest_base=mem.mem.disp;offset=0
    else:offset=mem.mem.disp-guest_base
   assert 0<=offset<=(32 if kind in (21,22,29,30,38,39,46,47,51,63,67,71,75) else 48 if kind in (7,12,16,17,25,26,34,35,42,43,50,53,62,66,70,74) else 56 if kind in (8,9,10,56,57,58,59) else 60)
  if 60<=kind<=75:
   edges=[0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x00800001,0x00800002,0x80800000,0x3f7fffff,0x3f800000,0xbf800000,0x3f800001,0x33800000,0x33000000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0xffc56789,0x7fa12345,0xffa56789]
   value=bytearray(initial);backing=bytearray(memory);wide=kind in (61,63,65,67,69,71,73,75)
   if site['origin']=='packed Cartesian reference' or iteration<32 or site['pc']:
    ai=iteration//384 if site['origin']=='packed Cartesian reference' else (iteration//16)%24 if site['pc'] else iteration%24
    bi=(iteration//16)%24 if site['origin']=='packed Cartesian reference' else (iteration//16*11+3)%24 if site['pc'] else (iteration*7+3)%24
    for lane in range(8 if wide else 4):
     av,bv=edges[(ai+7*lane)%24],edges[(bi+11*lane)%24];where=merge*16+lane*4 if lane<4 else 128+merge*16+(lane-4)*4
     value[where:where+4]=struct.pack('<I',av)
     if mem is not None:backing[offset+lane*4:offset+lane*4+4]=struct.pack('<I',bv)
     else:
      where=src*16+lane*4 if lane<4 else 128+src*16+(lane-4)*4;value[where:where+4]=struct.pack('<I',bv)
    initial=bytes(value);memory=bytes(backing)
  if arithmetic and kind not in (13,18):
   pairs=[(0,0x80000000),(0x3f800000,0xbf800000),(0x00800000,0x3f7fffff),(0x00800001,0x3f7ffffe),(1,0x3f800000),(0x007fffff,0x3f800000),(0x7f800000,0),(0xff800000,0x80000000),(0x7fc12345,0xffa56789),(0x7fa12345,0xffc56789),(0x7fc12345,1),(0x7f7fffff,0x40000000),(0xff7fffff,0x40000000),(0x3fc00000,0x3fc00000),(1,0x3f000000),(0x00800000,0xbf7fffff)]
   av,bv=pairs[iteration//16] if site['pc'] and iteration<256 else pairs[iteration%16] if iteration<16 else (rng.getrandbits(32),rng.getrandbits(32))
   if kind==6:
    edges=[0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x00800001,0x80800001,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc00001,0xffc00123,0x7f800001,0xff800123,0x7fc12345,0xffc56789,0x7fa12345,0xffa56789,0x7f7fffff,0xff7fffff,0x3f7fffff,0xbf7fffff,0x3f800001,0xbf800001,0x00800002,0x80800002,0x00400000,0x80400000]
    av=0x7fa12345 if iteration%2 else 1
    bv=edges[iteration//16] if site['pc'] or site['origin']=='widening edge matrix' else edges[iteration] if iteration<32 else rng.getrandbits(32)
   if site['origin']=='arithmetic Cartesian reference':
    values=[0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x00800001,0x00800002,0x3f7fffff,0x3f7ffffe,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc12345,0xffc56789,0x7fa12345,0xffa56789,0x7f7fffff,0xff7fffff]
    av,bv=values[iteration//320],values[(iteration//16)%20]
   value=bytearray(initial);value[merge*16:merge*16+4]=struct.pack('<I',av)
   if mem is None:value[src*16:src*16+4]=struct.pack('<I',bv)
   else:memory=bytearray(memory);memory[offset:offset+4]=struct.pack('<I',bv);memory=bytes(memory)
   initial=bytes(value)
  if kind==8:
   edges=[0,0x8000000000000000,1,0x8000000000000001,0x000fffffffffffff,0x0010000000000000,0x0010000000000001,0x8010000000000000,0x3fefffffffffffff,0xbfefffffffffffff,0x3ff0000000000000,0xbff0000000000000,0x7ff0000000000000,0xfff0000000000000,0x7ff8000000012345,0xfff8000000056789,0x7ff0000000012345,0xfff0000000056789,0x7fefffffffffffff,0xffefffffffffffff]
   av,bv=(edges[iteration//320],edges[(iteration//16)%20]) if site['origin']=='comparison edge matrix' else (edges[(iteration//16)%20],edges[((iteration//16)*7+3)%20]) if iteration<320 else (rng.getrandbits(64),rng.getrandbits(64))
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=320 and site['origin']!='comparison edge matrix' else 0)
   value=bytearray(initial);value[merge*16:merge*16+8]=struct.pack('<Q',av)
   if mem is None:value[src*16:src*16+8]=struct.pack('<Q',bv)
   else:memory=bytearray(memory);memory[offset:offset+8]=struct.pack('<Q',bv);memory=bytes(memory)
   initial=bytes(value)
  if square_root:
   edges=[0,0x8000000000000000,1,0x8000000000000001,0x000fffffffffffff,0x800fffffffffffff,0x0010000000000000,0x8010000000000000,0x0010000000000001,0x8010000000000001,0x3ff0000000000000,0xbff0000000000000,0x4000000000000000,0xc000000000000000,0x4010000000000000,0xc010000000000000,0x7ff0000000000000,0xfff0000000000000,0x7ff8000000012345,0xfff8000000056789,0x7ff0000000012345,0xfff0000000056789,0x7fefffffffffffff,0xffefffffffffffff,0x3fefffffffffffff,0x3ff0000000000001,0x400fffffffffffff,0x4010000000000001,0x0000000000000002,0x0008000000000000,0x4022000000000000,0x4024000000000000]
   bv=edges[iteration//16] if site['pc'] or site['origin']=='square root edge matrix' else edges[iteration] if iteration<32 else rng.getrandbits(64)
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=32 and not(site['pc'] or site['origin']=='square root edge matrix') else 0)
   value=bytearray(initial);value[merge*16:merge*16+8]=struct.pack('<Q',0x7ff0000000012345 if iteration%2 else 1)
   if mem is None:value[src*16:src*16+8]=struct.pack('<Q',bv)
   else:memory=bytearray(memory);memory[offset:offset+8]=struct.pack('<Q',bv);memory=bytes(memory)
   initial=bytes(value)
  if narrowing:
   magnitudes=[0,1,0x000fffffffffffff,0x0010000000000000,0x7ff0000000000000,0x7ff8000000012345,0x7ff0000000012345,0x7fefffffffffffff,0x3ff0000000000000,0x3ff0000000000001,0x3ff0000010000000,0x3ff0000010000001,0x3ff000000fffffff,0x3ff0000030000000,0x3690000000000000,0x3690000000000001,0x368fffffffffffff,0x36a0000000000000,0x380fffffc0000000,0x380fffffe0000000,0x380ffffff0000000,0x380ffffff0000001,0x380fffffefffffff,0x3810000000000000,0x380fffffffffffff,0x3810000000000001,0x47efffffe0000000,0x47effffff0000000,0x47efffffefffffff,0x47effffff0000001,0x47f0000000000000,0x4000000000000000]
   edges=[v|sign for v in magnitudes for sign in [0,0x8000000000000000]]
   bv=edges[iteration//16] if site['pc'] or site['origin']=='narrowing edge matrix' else edges[iteration] if iteration<64 else rng.getrandbits(64)
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=64 and not(site['pc'] or site['origin']=='narrowing edge matrix') else 0)
   value=bytearray(initial);value[merge*16:merge*16+8]=struct.pack('<Q',0x7ff0000000012345 if iteration%2 else 1)
   if mem is None:value[src*16:src*16+8]=struct.pack('<Q',bv)
   else:memory=bytearray(memory);memory[offset:offset+8]=struct.pack('<Q',bv);memory=bytes(memory)
   initial=bytes(value)
  if ordered_comparison:
   edges=[0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x00800001,0x80800000,0x3f7fffff,0xbf7fffff,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc12345,0xffc56789,0x7fa12345,0xffa56789,0x7f7fffff,0xff7fffff]
   av,bv=(edges[iteration//320],edges[(iteration//16)%20]) if site['origin']=='ordered comparison edge matrix' else (edges[(iteration//16)%20],edges[((iteration//16)*7+3)%20]) if iteration<320 else (rng.getrandbits(32),rng.getrandbits(32))
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=320 and site['origin']!='ordered comparison edge matrix' else 0)
   value=bytearray(initial);value[merge*16:merge*16+4]=struct.pack('<I',av)
   if mem is None:value[src*16:src*16+4]=struct.pack('<I',bv)
   else:memory=bytearray(memory);memory[offset:offset+4]=struct.pack('<I',bv);memory=bytes(memory)
   initial=bytes(value)
  if kind==13:
   pairs=[(0,0),(0x80000000,0),(1,0),(0x3f800000,0),(0x7f800000,0),(0x7f800000,0x7f800000),(0,1),(1,0x7f800000),(0x7fc12345,1),(0x7fa12345,0xffc56789),(1,1),(0x7f7fffff,1),(0x00800000,0x7f7fffff),(0x7f7fffff,0x3f000000),(0x00800000,0x3f800001),(1,0x40000000),(0x3f800000,0x40400000),(1,0x00800000),(0x007fffff,0x3f7fffff),(0x00800001,0x3f800001),(0xbf800000,0),(0x3f800000,0x80000000),(0xff800000,0x7f800000),(0x7fc12345,0xffa56789),(0x7fa12345,0xffa56789),(0xffc12345,0x7fc56789),(0x80000001,0x80000000),(0x00800000,0x3f7fffff),(0x00800001,0x3f800002),(0x7f800000,1),(0x3f800001,0x3f800000),(0x3f7fffff,0x3f800000)]
   edges=[0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x00800001,0x00800002,0x80800000,0x3f7fffff,0x3f800000,0xbf800000,0x3f800001,0x3f800002,0x40000000,0x40400000,0x3f000000,0x7f800000,0xff800000,0x7fc12345,0xffc56789,0x7fa12345,0xffa56789]
   av,bv=(edges[iteration//384],edges[(iteration//16)%24]) if site['origin']=='division Cartesian reference' else pairs[iteration//16] if site['pc'] else pairs[iteration] if iteration<32 else (rng.getrandbits(32),rng.getrandbits(32))
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=32 and not(site['pc'] or site['origin']=='division Cartesian reference') else 0)
   value=bytearray(initial);value[merge*16:merge*16+4]=struct.pack('<I',av)
   if mem is None:value[src*16:src*16+4]=struct.pack('<I',bv)
   else:memory=bytearray(memory);memory[offset:offset+4]=struct.pack('<I',bv);memory=bytes(memory)
   initial=bytes(value)
  if kind==18:
   pairs=[(0,0),(0x80000000,0),(0,0x80000000),(0x80000000,0x80000000),(1,1),(0x7f800000,0x7f800000),(0x7f800000,0xff800000),(0xff800000,0xff800000),(0x7fc12345,1),(0x7fa12345,0xffc56789),(0x3f800000,0x3f800000),(0x7f7fffff,0xff7fffff),(0x00800000,0x007fffff),(0x007fffff,0x00800000),(1,0),(0x3f800000,0x33000000),(0x3f800000,0x33800000),(1,0x00800000),(0x007fffff,0x807fffff),(0x00800001,0x00800000),(0xbf800000,0xbf800000),(0x3f800000,0xbf800000),(0xff800000,0x7f800000),(0x7fc12345,0xffa56789),(0x7fa12345,0xffa56789),(0xffc12345,0x7fc56789),(0x80000001,1),(0x00800000,0x00800001),(0x00800002,0x00800001),(0x7f800000,1),(0x3f800001,0x3f800000),(0x3f7fffff,0x3f800000)]
   edges=[0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x00800001,0x00800002,0x80800000,0x3f7fffff,0x3f800000,0xbf800000,0x3f800001,0x33800000,0x33000000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0xffc56789,0x7fa12345,0xffa56789]
   av,bv=(edges[iteration//384],edges[(iteration//16)%24]) if site['origin']=='subtraction Cartesian reference' else pairs[iteration//16] if site['pc'] else pairs[iteration] if iteration<32 else (rng.getrandbits(32),rng.getrandbits(32))
   control=0x1f80|((iteration%4)<<13)|(0x40 if iteration&4 else 0)|(0x8000 if iteration&8 else 0)|(rng.randrange(64) if iteration>=32 and not(site['pc'] or site['origin']=='subtraction Cartesian reference') else 0)
   value=bytearray(initial);value[merge*16:merge*16+4]=struct.pack('<I',av)
   if mem is None:value[src*16:src*16+4]=struct.pack('<I',bv)
   else:memory=bytearray(memory);memory[offset:offset+4]=struct.pack('<I',bv);memory=bytes(memory)
   initial=bytes(value)
  mapped=vector(initial,dest)+vector(initial,merge)+vector(initial,src)
  if mem is not None and kind in (21,29,38,46,51,63,67,71,75):mapped=mapped[:64]+memory[offset:offset+32]
  elif mem is not None and kind in (7,12,16,25,34,42,50,62,66,70,74):mapped=mapped[:64]+memory[offset:offset+16]+mapped[80:]
  elif ordered_comparison and mem is not None:mapped=mapped[:64]+memory[offset:offset+4]+mapped[68:]
  elif (comparison or square_root or narrowing or kind in (56,57)) and mem is not None:mapped=mapped[:64]+memory[offset:offset+8]+mapped[72:]
  elif arithmetic and mem is not None:mapped=mapped[:64]+memory[offset:offset+4]+mapped[68:]
  word=struct.unpack_from('<I',memory,offset)[0]
  requests.append(f'{kind} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for v in struct.unpack('<12Q',mapped))+f' {immediate:x}\n')
  cases.append((index,0x202|flags,control,regs,initial,memory,dest,kind,offset,mapped,guest_base,immediate))
oracle=R/'tests/vmovss_oracle.c';reference=out/'oracle-x86'
subprocess.run(['clang','-arch','x86_64','-mavx','-mno-red-zone','-O2','-Wall','-Wextra','-Werror',str(oracle),'-o',str(reference)],check=True)
request=''.join(requests);(out/'requests.txt').write_text(request)
run=subprocess.run(['arch','-x86_64',str(reference)],input=request,text=True,capture_output=True,check=True,timeout=60)
(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(cases)
def array(raw):return '{'+','.join(map(str,raw))+'}'
rows=[]
for case,reply in zip(cases,replies):
 index,flags,control,regs,initial,memory,dest,kind,offset,mapped,guest_base,immediate=case
 values=[int(v,16) for v in reply.split()];observed=struct.pack('<12Q',*values[:12]);after_word,after_control,after_flags=values[12:15];packed=struct.pack('<4Q',*values[15:]);assert packed==(mapped[64+(immediate&1)*16:80+(immediate&1)*16]+mapped[80:96] if kind==53 else mapped[64+(8 if kind==59 else 0):72+(8 if kind==59 else 0)]+mapped[72:96] if kind in (58,59) else mapped[64:96])
 assert (after_control==control if kind<4 or kind in (7,12,14,15,16,17,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59) else after_control&~0x3f==control&~0x3f and after_control&control&0x3f==control&0x3f) and (kind in (8,11) or after_flags==(flags&0x8d5))
 wanted=bytearray(mapped);wanted_word=struct.unpack_from('<I',memory,offset)[0]
 if 60<=kind<=75:
  wanted[:16 if kind in (60,62,64,66,68,70,72,74) else 32]=observed[:16 if kind in (60,62,64,66,68,70,72,74) else 32]
  if kind in (60,62,64,66,68,70,72,74):wanted[16:32]=bytes(16)
 elif kind in (54,55,56,57):
  wanted[:16]=(mapped[72:80]+mapped[40:48] if kind==54 else mapped[32:40]+mapped[64:72] if kind in (55,57) else mapped[64:72]+mapped[40:48]);wanted[16:32]=bytes(16)
 elif kind==52:
  wanted[:16]=mapped[64+(immediate&1)*16:80+(immediate&1)*16];wanted[16:32]=bytes(16)
 elif kind in (48,49,50,51):
  result=bytearray(32)
  for block in range(2 if kind in (49,51) else 1):
   selectors=[(immediate>>(2*lane))&3 for lane in range(4)]
   for lane,selector in enumerate(selectors):
    start=(32 if lane<2 else 64)+block*16+selector*4
    result[block*16+lane*4:block*16+lane*4+4]=mapped[start:start+4]
  wanted[:32]=result
 elif kind==31:
  for lane in range(3):wanted[lane*32+16:lane*32+32]=bytes(16)
 elif kind in (8,11,17,22,26,30,35,39,43,47,53,58,59):pass
 elif kind in (19,20,21,27,28,29,36,37,38,44,45,46):wanted[:32]=mapped[64:96]
 elif kind in (14,15,16,23,24,25,32,33,34,40,41,42):wanted[:16]=mapped[64:80];wanted[16:32]=bytes(16)
 elif kind in (7,12):wanted[:16]=bytes(a^b for a,b in zip(mapped[32:48],mapped[64:80]));wanted[16:32]=bytes(16)
 elif kind in (6,9):wanted[:8]=observed[:8];wanted[8:16]=mapped[40:48];wanted[16:32]=bytes(16)
 elif kind in (0,1,4,5,10,13,18):wanted[:4]=observed[:4] if kind>=4 else mapped[64:68];wanted[4:16]=mapped[36:48];wanted[16:32]=bytes(16)
 elif kind==2:wanted[:4]=struct.pack('<I',wanted_word);wanted[4:32]=bytes(28)
 else:wanted_word=struct.unpack('<I',mapped[64:68])[0]
 assert observed==bytes(wanted) and after_word==wanted_word
 expected=bytearray(initial);after=bytearray(memory)
 if kind==31:expected[128:256]=bytes(128)
 elif kind not in (3,8,11,17,22,26,30,35,39,43,47,53,58,59):expected[dest*16:dest*16+16]=observed[:16];expected[128+dest*16:144+dest*16]=observed[16:32]
 elif kind==3:after[offset:offset+4]=struct.pack('<I',after_word)
 elif kind in (58,59):after[offset:offset+8]=packed[:8]
 elif kind in (17,26,35,43,53):after[offset:offset+16]=packed[:16]
 elif kind in (22,30,39,47):after[offset:offset+32]=packed
 rows.append(struct.pack('<14I',index,flags,control,after_control,0x202|after_flags,*regs,guest_base)+initial+expected+memory+after)
table=out/'cases.bin';table.write_bytes(b''.join(rows));assert table.stat().st_size==len(cases)*696
if a.widen and (out/'static-array-proof.json').exists():
 proof=json.loads((out/'static-array-proof.json').read_text());assert proof.get('record_bytes')!=696 or proof['embedded_array_sha256']==sha(table),'binary fixture differs from passing static array'
(out/'bodies.inc').write_text(''.join(f'extern void test_{i}(_cpu *);\n' for i in range(len(sites)))+'static void (*const bodies[])(_cpu *)={'+','.join(f'test_{i}' for i in range(len(sites)))+'};\nstatic const unsigned char unmasked_move[]={'+','.join('1' if site['assembly'].startswith(('vmovups ','vzeroupper ','vmovdqa ','vmovdqu ','vshufps ','vextractf128 ','vmovhlps ','vmovlhps ','vmovlps ','vmovhps ')) else '0' for site in sites)+'};\nstatic const unsigned char alignment_probe[]={'+','.join('1' if site['origin']=='packed integer alignment fault probe' else '0' for site in sites)+'};\n')
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
sources=[R/'port/llasm-support/tribes_sse.c',R/'tests/avx_ss_lowering_test.c'];math=sse_inputs('simulator' if a.simulator_device else 'macos');runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 run=subprocess.run(cmd+math['compile_flags']+[str(ir_path)]+list(map(str,sources))+math['sources']+math['libraries']+['-o',str(exe)],capture_output=True,text=True)
 (out/(mode+'-compile.log')).write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe),str(table)] if a.simulator_device else [str(exe),str(table)],capture_output=True,text=True,timeout=300 if mode=='tsan' else 120)
 (out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':sha(exe)})
paths=sources+[R/'tests/vshufps_oracle.inc',R/'tests/vextractf128_oracle.inc']+[R/'port/llasm-support'/n for n in ['llasm_cpu.h','tribes_sse.h','tribes_sse.llinc']]+[R/'scripts/patches/srw-tribes.patch',R/'scripts/sse_instruction_fixture.py',pathlib.Path(__file__).resolve()]
report={'state':'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'cases':len(cases),'register_cases':register_cases,'memory_cases':len(cases)-register_cases,'original_sites':27 if a.packed_divide else 26 if a.packed_add else 25 if a.packed else 23 if a.half else 21 if a.extract else 20 if a.shuffle else 19 if a.dq else 18 if a.zero else 17 if a.ups else 16 if a.aps256 else 15 if a.subtract else 14 if a.aps else 13 if a.divide else 12 if a.xorps else 11 if a.comiss else 10 if a.narrow else 9 if a.sqrt else 8 if a.compare else 7 if a.xor else 6 if a.widen else 5 if a.scalar else 2,'fixture_batches':fixture_batches,'emitted_blocks':len(sites),'sites':sites,'negative':negative,'runs':runs,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'corpus_format':'Little-endian 696-byte Case records, read-only mmap; native ABI asserted','corpus_sha256':sha(table),'emitted_llasm_sha256':sha(source),'llvm_sha256':sha(ir_path),'srw_sha256':sha(srw),'llasm_sha256':sha(llasm),'original_image_sha256':sha(original),'oracle':{'kind':'Offline canonical VEX scalar instructions through Rosetta with mapped guest roles','source_sha256':sha(oracle),'immediate_table_sha256':sha(R/'tests/vshufps_oracle.inc'),'extract_table_sha256':sha(R/'tests/vextractf128_oracle.inc'),'binary_sha256':sha(reference),'request_sha256':sha(out/'requests.txt'),'results_sha256':sha(out/'reference.txt')},'sse_math':math['evidence'],'unmasked_move_cases':sum(23<=case[7]<=30 for case in cases),'zero_upper_cases':sum(case[7]==31 for case in cases),'packed_integer_cases':sum(32<=case[7]<=47 for case in cases),'packed_arithmetic_cases':sum(60<=case[7]<=75 for case in cases),'packed_enabled':a.packed,'packed_add_enabled':a.packed_add,'packed_divide_enabled':a.packed_divide,'packed_divide_cases':sum(72<=case[7]<=75 for case in cases),'packed_add_cases':sum(68<=case[7]<=71 for case in cases),'half_cases':sum(54<=case[7]<=59 for case in cases),'half_enabled':a.half,'extract_cases':sum(52<=case[7]<=53 for case in cases),'extract_enabled':a.extract,'shuffle_cases':sum(48<=case[7]<=51 for case in cases),'shuffle_enabled':a.shuffle,'dq_enabled':a.dq,'zero_enabled':a.zero,'ups_enabled':a.ups,'aps256_enabled':a.aps256,'subtract_enabled':a.subtract,'aps_enabled':a.aps,'divide_enabled':a.divide,'xorps_enabled':a.xorps,'comiss_enabled':a.comiss,'narrow_enabled':a.narrow,'sqrt_enabled':a.sqrt,'compare_enabled':a.compare,'xor_enabled':a.xor,'widen_enabled':a.widen,'scalar_enabled':a.scalar,'scope':'Actual strict AOT VEX scalar bodies for default flat IA-32 addresses and C5/C4 WIG encodings; no precise fault delivery, other VEX opcodes, whole DLL execution, physical Intel or gameplay claim'}
report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='sites'},indent=2))
