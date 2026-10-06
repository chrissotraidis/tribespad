"""Strict isolated original-byte PE fixture for Tribes SRW instruction tests."""
import os,struct,subprocess
import pefile

def fixture(directory,records,original,srw):
 directory.mkdir(parents=True,exist_ok=True);raw=bytearray(original.read_bytes()[:0x400]);header=struct.unpack_from('<I',raw,0x3c)[0];optional=header+24;code=bytearray();aliases=[]
 for i,site in enumerate(records):
  instruction=bytes.fromhex(site['bytes_hex']);assert len(instruction)<31;code+=instruction+b'\xc3'+bytes(31-len(instruction));aliases.append(f'loc_{0x10001000+32*i:X},fixture_{i}')
 span=(len(code)+511)&~511;struct.pack_into('<H',raw,header+6,1);struct.pack_into('<H',raw,header+22,0x102);struct.pack_into('<I',raw,optional+4,span);struct.pack_into('<I',raw,optional+16,0x1000);struct.pack_into('<I',raw,optional+56,(0x1000+span+4095)&~4095);struct.pack_into('<I',raw,optional+60,0x400);raw[optional+96:optional+224]=bytes(128);section=optional+224;raw[section:]=bytes(len(raw)-section);struct.pack_into('<8s6I2HI',raw,section,b'.text\0\0\0',len(code),0x1000,span,0x400,0,0,0,0,0x60000020);raw+=code+bytes(span-len(code));(directory/'fixture.exe').write_bytes(raw);(directory/'relocations.csv').write_text('');(directory/'global_aliases.sci').write_text('\n'.join(aliases)+'\n')
 decoded=pefile.PE(data=bytes(raw));assert decoded.OPTIONAL_HEADER.ImageBase==0x10000000
 for i,site in enumerate(records):assert decoded.get_data(0x1000+32*i,len(bytes.fromhex(site['bytes_hex']))).hex()==site['bytes_hex']
 run=subprocess.run([str(srw),'fixture.exe','fixture.llasm'],cwd=directory,env={k:v for k,v in {**os.environ,'TRIBES_SRW_FULL_FLAGS':'1'}.items() if k!='HALOPAD_SRW_DIAG'},capture_output=True,text=True,timeout=60);(directory/'srw.log').write_text(run.stdout+run.stderr);return run
