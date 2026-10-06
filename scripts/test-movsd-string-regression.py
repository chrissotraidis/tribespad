#!/usr/bin/env python3
"""Execute actual strict string MOVSD/REP bodies against original x86 states."""
import hashlib,json,os,pathlib,re,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/sse/movsd-string-regression';out.mkdir(exist_ok=True);srw=R/'generated/tools/SRW-tribes/SRW.exe';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
raw=bytearray((R/'ref/product/IFC22.dll').read_bytes()[:0x400]);header=struct.unpack_from('<I',raw,0x3c)[0];optional=header+24;code=bytearray();forms=['a5','f3a5']
for x in forms:b=bytes.fromhex(x);code+=b+b'\xc3'+bytes(31-len(b))
span=512;struct.pack_into('<H',raw,header+6,1);struct.pack_into('<H',raw,header+22,0x102)
for offset,value in [(4,span),(16,0x1000),(56,0x2000),(60,0x400)]:struct.pack_into('<I',raw,optional+offset,value)
raw[optional+96:optional+224]=bytes(128);section=optional+224;raw[section:]=bytes(len(raw)-section);struct.pack_into('<8s6I2HI',raw,section,b'.text\0\0\0',len(code),0x1000,span,0x400,0,0,0,0,0x60000020);raw+=code+bytes(span-len(code));(out/'fixture.exe').write_bytes(raw);(out/'relocations.csv').write_text('');(out/'global_aliases.sci').write_text('loc_10001000,fixture_0\nloc_10001020,fixture_1\n')
run=subprocess.run([str(srw),'fixture.exe','fixture.llasm'],cwd=out,env={k:v for k,v in {**os.environ,'TRIBES_SRW_FULL_FLAGS':'1'}.items() if k!='HALOPAD_SRW_DIAG'},capture_output=True,text=True,check=True);(out/'srw.log').write_text(run.stdout+run.stderr)
text=(out/'seg01_code.llinc').read_text();bodies=[]
for i in range(2):
 body=re.search(r'^proc fixture_'+str(i)+r' global[^\n]*\n(.*?)^endp$',text,re.M|re.S)[1].split(';ret\n')[0];assert 'tb_sse' not in body;bodies.append(body)
assert 'load tmp1, esi' in bodies[0] and 'store tmp1, edi' in bodies[0] and 'REP_MOVSD' in bodies[1]
llasm=out/'blocks.llasm';llasm.write_text('include llasm_movs.llinc\ndefine DF 1024\n'+''.join('proc test_%d public\n%sendp\n'%(i,b) for i,b in enumerate(bodies)))
ir=out/'blocks.ll';subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(ir),str(llasm)],check=True,capture_output=True);ir.write_text(ir.read_text().replace('define hidden fastcc void @test_','define ccc void @test_'))
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x1000,4096);u.mem_map(0x2000,4096);rows=[]
for form,hexcode in enumerate(forms):
 for direction in [0,1]:
  for count in [0,1,2,7,16]:
   for misalign in range(4):
    for overlap in [-4,0,4,256]:
     source=0x2180+misalign;dest=source+overlap;flags=0x8d7|(direction<<10);data=bytes((i*71+23)&255 for i in range(1024));u.mem_write(0x2000,data)
     u.reg_write(X.UC_X86_REG_ESI,source);u.reg_write(X.UC_X86_REG_EDI,dest);u.reg_write(X.UC_X86_REG_ECX,count);u.reg_write(X.UC_X86_REG_EFLAGS,flags)
     b=bytes.fromhex(hexcode);u.mem_write(0x1000,b);u.emu_start(0x1000,0x1000+len(b));after=bytes(u.mem_read(0x2000,1024));regs=[u.reg_read(r) for r in [X.UC_X86_REG_ESI,X.UC_X86_REG_EDI,X.UC_X86_REG_ECX,X.UC_X86_REG_EFLAGS]]
     rows.append('{'+','.join(map(str,[form,source,dest,count,flags,*regs]))+',{'+','.join(map(str,after))+'}}')
(out/'cases.inc').write_text('static const Case cases[]={\n'+',\n'.join(rows)+'\n};\n')
source=R/'tests/movsd_string_regression_test.c';exe=out/'test';support=R/'ref/halopad/port/llasm-support/llasm_movs.c'
subprocess.run(['clang','-O2','-DPTROFS_64BIT','-Wno-override-module','-I',str(out),str(source),str(ir),str(support),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,check=True);assert run.stdout.startswith('PASS:') and not run.stderr
(out/'result.json').write_text(json.dumps(dict(passed=True,cases=len(rows),output=run.stdout,srw_sha256=sha(srw),sources_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),source,support,R/'ref/halopad/port/llasm-support/llasm_movs.llinc',R/'ref/halopad/port/llasm-support/llasm_cpu.h']},binary_sha256=sha(exe)),indent=2)+'\n');print(run.stdout,end='')
