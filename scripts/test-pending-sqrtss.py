#!/usr/bin/env python3
"""Check the one added SQRTSS helper against canonical x86 and integer rounding."""
import hashlib,json,pathlib,random,subprocess
from f32_sqrt_reference import root
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/pending-hook-bank/sqrtss';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();rng=random.Random(25034)
source=R/'tests/legacy_sqrtps_oracle.c';oracle=out/'oracle.c';oracle.write_text(source.read_text().replace('sqrtps','sqrtss'));exe=out/'oracle'
subprocess.run(['clang','-arch','x86_64','-mavx','-mno-red-zone','-O2',str(oracle),'-o',str(exe)],check=True)
edge=[0,0x80000000,1,0x80000001,0x7fffff,0x800000,0x3f800000,0xbf800000,0x40000000,0x40800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0x7fa12345];cases=[];requests=[]
for x in edge+[rng.getrandbits(32) for _ in range(48)]:
 for bits in range(16):
  control=0x1f80|((bits&3)<<13)|(64 if bits&4 else 0)|(0x8000 if bits&8 else 0)|(bits&63);result,status=root(x,control);dest=[rng.getrandbits(32) for _ in range(8)];src=[x]+[rng.getrandbits(32) for _ in range(7)];flags=rng.getrandbits(32)&0x8d5
  requests.append('0 %x %x '%(control,flags)+' '.join('%x'%v for v in dest+src)+'\n');cases.append((control,x,result,control|status,flags,dest))
run=subprocess.run([str(exe)],input=''.join(requests),capture_output=True,text=True,check=True,timeout=30)
for c,line in zip(cases,run.stdout.splitlines()):
 control,x,result,after,flags,dest=c;assert [int(x,16) for x in line.split()]==[after,flags,result]+dest[1:]
assert len(run.stdout.splitlines())==len(cases)
(out/'cases.inc').write_text('static const uint32_t cases[][4]={'+','.join('{%du,%du,%du,%du}'%c[:4] for c in cases)+'};\n')
c=out/'test.c';c.write_text('''#include "llasm_cpu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cases.inc"
extern void tb_sse_sqrtss(_cpu *,unsigned,unsigned);
int main(void){for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)for(unsigned src=1;src<=2;src++){
 _cpu cpu,expected;for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)(j*73+i);
 cpu._mxcsr=cases[i][0];memcpy(cpu._xmm[src],&cases[i][1],4);expected=cpu;expected._mxcsr=cases[i][3];memcpy(expected._xmm[1],&cases[i][2],4);
 tb_sse_sqrtss(&cpu,1,src);assert(!memcmp(&cpu,&expected,sizeof cpu));
}puts("PASS: 2048 SQRTSS complete-CPU cases");return 0;}
''')
math=sse_inputs('macos');helper=R/'port/llasm-support/tribes_sqrtss.c';native=out/'native';subprocess.run(['clang','-arch','arm64','-O2','-DPTROFS_64BIT','-I',str(R/'port/llasm-support'),'-I',str(out),*math['compile_flags'],str(c),str(helper),*math['sources'],*math['libraries'],'-o',str(native)],check=True)
run=subprocess.run([str(native)],capture_output=True,text=True,check=True);print(run.stdout.strip())
report=dict(passed=True,cases=len(cases)*2,helper_sha256=sha(helper),script_sha256=sha(pathlib.Path(__file__).resolve()),oracle_source_sha256=sha(source),binary_sha256=sha(native),scope='Legacy register SQRTSS against fresh Rosetta opcode and independent integer rounding; 440-byte CPU comparison includes aliasing, upper lanes, flags and sticky MXCSR. Existing unmasked exception stop retained.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
