#!/usr/bin/env python3
"""Prove real FS addressing, pointer dereference, stack width and null isolation."""
import hashlib,json,pathlib,struct
import unicorn
from unicorn import x86_const as X
from guest_teb_contract import configure_sparse_teb,DECLARED_FIELDS
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/teb-contract';out.mkdir(exist_ok=True)
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for base,size in [(0x10000,4096),(0x100000,0x200000),(0x3a0000,0x40000)]:u.mem_map(base,size)
cases=[]
for teb,gdt,tid in [(0x3d0000,0x3c0000,2001),(0x3b0000,0x3a0000,2002)]:
 profile=configure_sparse_teb(u,teb,gdt,0x300000,0x100000,1234,tid,0x3e0000)
 for offset,expected in zip(DECLARED_FIELDS,(0xffffffff,0x300000,0x100000,teb,1234,tid,0x3e0000)):
  u.mem_write(0x10000,b'\x64\xa1'+struct.pack('<I',offset));u.reg_write(X.UC_X86_REG_EFLAGS,0x8d7);u.emu_start(0x10000,0x10006)
  assert u.reg_read(X.UC_X86_REG_EAX)==expected and u.reg_read(X.UC_X86_REG_EFLAGS)==0x8d7
  cases.append({'teb':teb,'offset':offset,'value':expected})
 # FS self is also a usable linear pointer; the pointer follows the same TIB.
 u.mem_write(0x10000,bytes.fromhex('64a1180000008b5004'));u.emu_start(0x10000,0x10009);assert u.reg_read(X.UC_X86_REG_EDX)==0x300000
 # Real SEH-head write, then push/pop FS:[0] with ESP above 64 KiB.
 u.reg_write(X.UC_X86_REG_EAX,0x2fff20);u.mem_write(0x10000,bytes.fromhex('64a30000000064ff3500000000648f0500000000'));u.reg_write(X.UC_X86_REG_ESP,0x2fff00);u.emu_start(0x10000,0x10014)
 assert u.reg_read(X.UC_X86_REG_ESP)==0x2fff00 and struct.unpack('<I',u.mem_read(teb,4))[0]==0x2fff20
 assert u.reg_read(X.UC_X86_REG_FS_BASE)==teb and u.reg_read(X.UC_X86_REG_SS)==16
try:u.mem_read(0x18,4)
except unicorn.UcError:pass
else:raise AssertionError('default sparse TEB accidentally exposes the null page')
for teb in [0,1,0x80000000]:
 try:configure_sparse_teb(u,teb,0x3c0000,0x300000,0x100000,1234,2001,0x3e0000)
 except ValueError:pass
 else:raise AssertionError('invalid identity accepted')
report={'pass':True,'unicorn_version':unicorn.__version__,'cases':cases,'checks':['two distinct nonzero FS self identities','self pointer dereferences actual TIB','seven declared DWORD fields','MOV retains arithmetic flags','FS write/push/pop retain 32-bit ESP and head bytes','default null page remains unmapped','invalid identities rejected'],'source_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [R/'scripts/guest_teb_contract.py',R/'scripts/test-teb-contract.py']},'scope':'Original-x86 segment/loader contract only; native sparse FS helper tested separately'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: real nonzero FS identities, flat 32-bit stack, TIB pointer/head coherence and null-page isolation')
