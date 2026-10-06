#!/usr/bin/env python3
"""Strictly translate the exact input teardown block after its six NOP patch."""
import hashlib,json,os,pathlib,re,struct,subprocess
import pefile
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/startup-input-variant';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original=R/'ref/control/Tribes2.exe';assert sha(original)=='7ad1d40a6aa74a25d089af6bd6f34fcc20a172fd5932ded82c69fca673e7f8f9'
pe=pefile.PE(str(original));assert pe.get_data(0x156cff,14).hex()=='50ff15f0c67d00c7464c00000000'
# Keep the original guest addresses. The second boundary is an explicit seed,
# so the replacement block ends before the next unchanged store instruction.
raw=bytearray(original.read_bytes()[:0x400]);header=struct.unpack_from('<I',raw,0x3c)[0];optional=header+24
struct.pack_into('<H',raw,header+6,1);struct.pack_into('<H',raw,header+22,0x102)
for offset,value in [(4,4096),(16,0x156cff),(56,0x157000),(60,0x400)]:struct.pack_into('<I',raw,optional+offset,value)
raw[optional+96:optional+224]=bytes(128);section=optional+224;raw[section:]=bytes(len(raw)-section);struct.pack_into('<8s6I2HI',raw,section,b'.text\0\0\0',4096,0x156000,4096,0x400,0,0,0,0,0x60000020)
code=bytearray(b'\xcc'*4096);code[0xcff:0xd07]=b'\x50'+b'\x90'*6+b'\xc3';raw+=code
(out/'fixture.exe').write_bytes(raw);(out/'relocations.csv').write_text('');(out/'global_aliases.sci').write_text('loc_556CFF,patched_input_block\nloc_556D06,unchanged_next\n')
srw=R/'generated/tools/SRW-tribes/SRW.exe';run=subprocess.run([str(srw),'fixture.exe','fixture.llasm'],cwd=out,env={k:v for k,v in {**os.environ,'TRIBES_SRW_FULL_FLAGS':'1'}.items() if k!='HALOPAD_SRW_DIAG'},capture_output=True,text=True,check=True);(out/'srw.log').write_text(run.stdout+run.stderr)
s=(out/'seg01_code.llinc').read_text();m=re.search(r'^proc patched_input_block global[^\n]*\n(.*?)^endp$',s,re.M|re.S);assert m;s=m[1];s=s.replace('tcall loc_556D06','tcall unchanged_next');assert 'tcall unchanged_next' in s and s.count(';nop')==6 and 'FreeLibrary' not in s
(out/'variant.llasm').write_text('include llasm.llinc\nproc unchanged_next external\nproc patched_input_block public\n'+s+'endp\n')
ir=out/'variant.ll';subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(ir),str(out/'variant.llasm')],capture_output=True,check=True)
body=ir.read_text();assert 'define hidden fastcc void @patched_input_block' in body
(out/'result.json').write_text(json.dumps(dict(state='strict_unpublished_variant',original_sha256=sha(original),srw_sha256=sha(srw),fixture_sha256=sha(out/'fixture.exe'),llasm_sha256=sha(out/'variant.llasm'),llvm_sha256=sha(ir),entry=0x556cff,continuation=0x556d06,patch_address=0x556d00,patch_before='ff15f0c67d00',patch_after='909090909090',script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Strict original-address block, one PUSH and six NOPs. Not linked or published until exact runtime writes and variant routing are implemented.'),indent=2)+'\n');print('Prepared strict original-address input teardown variant')
