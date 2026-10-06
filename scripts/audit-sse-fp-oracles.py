#!/usr/bin/env python3
"""Compare independent offline SSE references before native FP lowering.
Rosetta is a reference implementation, not proof of physical Intel semantics.
"""
import hashlib,json,pathlib,random,struct,subprocess
import capstone,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/sse/fp-oracles';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=R/'tests/sse_fp_oracle.c';binary=out/'oracle-x86'
subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
ops=[('addsd','f20f58c1'),('subsd','f20f5cc1'),('mulss','f30f59c1'),('cvtpd2ps','660f5ac1'),('cvttss2si','f30f2cc1'),('ucomisd','660f2ec1')]
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for name,raw in ops:
 ins=next(decoder.disasm(bytes.fromhex(raw),0x1000));assert ins.mnemonic==name and ins.size==4
# Zero signs, both infinities, QNaN/SNaN signs/payloads, subnormal boundaries,
# overflow/underflow, conversion limits and nearest-even ties, plus random bits.
double=[0,0x8000000000000000,0x3ff0000000000000,0xbff0000000000000,
 0x7ff0000000000000,0xfff0000000000000,0x7ff8000000000001,0xfff8000000000123,
 0x7ff0000000000001,0xfff0000000000123,1,0x8000000000000001,
 0x000fffffffffffff,0x0010000000000000,0x7fefffffffffffff,
 0x3ff0000010000000,0x3ff0000030000000,0x3800000000000000]
single=[0,0x80000000,0x3f800000,0xbf800000,0x7f800000,0xff800000,
 0x7fc00001,0xffc00123,0x7f800001,0xff800123,1,0x80000001,
 0x007fffff,0x00800000,0x7f7fffff,0x4f000000,0xcf000000,0x4effffff,
 0x3fc00000,0xbfc00000]
rng=random.Random(25034);cases=[]
for op,(name,raw) in enumerate(ops):
 values=single if name in ('mulss','cvttss2si') else double
 pairs=[(a,b) for a in values for b in values]+[(rng.getrandbits(32 if values is single else 64),rng.getrandbits(32 if values is single else 64)) for _ in range(64)]
 for daz_ftz in (0,0x40,0x8000,0x8040):
  for rc in range(4):
   for i,(a,b) in enumerate(pairs):
    # Exercise preserved upper scalar lanes, independently converted second
    # CVTPD2PS lane, and preexisting sticky status bits.
    if values is single:a|=rng.getrandbits(32)<<32;b|=rng.getrandbits(32)<<32
    a1=rng.getrandbits(64);b1=double[i%len(double)]
    control=0x1f80|(rc<<13)|daz_ftz|(0x3f if i&1 else 0)
    flags=(0,0x8d5,0x41,0x884)[i%4]
    cases.append((op,control,flags,a,a1,b,b1))
request=''.join(f'{op} {m:x} {f:x} {a:016x} {a1:016x} {b:016x} {b1:016x}\n' for op,m,f,a,a1,b,b1 in cases)
(out/'requests.txt').write_text(request)
run=subprocess.run(['arch','-x86_64',str(binary)],input=request,text=True,capture_output=True,check=True,timeout=120)
(out/'rosetta-results.txt').write_text(run.stdout)
actual=[tuple(int(x,16) for x in line.split()) for line in run.stdout.splitlines()];assert len(actual)==len(cases)
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x1000,4096)
report={name:{'cases':0,'differences':{'destination':0,'integer':0,'mxcsr':0,'eflags':0},'examples':[]} for name,_ in ops}
for case,reference in zip(cases,actual):
 op,m,f,a,a1,b,b1=case;name,raw=ops[op]
 u.mem_write(0x1000,bytes.fromhex(raw));u.ctl_remove_cache(0x1000,0x1004)
 u.reg_write(X.UC_X86_REG_XMM0,a|(a1<<64));u.reg_write(X.UC_X86_REG_XMM1,b|(b1<<64));u.reg_write(X.UC_X86_REG_EAX,0)
 u.reg_write(X.UC_X86_REG_MXCSR,m);u.reg_write(X.UC_X86_REG_EFLAGS,0x202|f);u.emu_start(0x1000,0x1004,count=1)
 dst=u.reg_read(X.UC_X86_REG_XMM0)
 observed=(dst&((1<<64)-1),dst>>64,u.reg_read(X.UC_X86_REG_EAX),u.reg_read(X.UC_X86_REG_MXCSR),u.reg_read(X.UC_X86_REG_EFLAGS)&0x8d5)
 r=report[name];r['cases']+=1
 diff=[label for label,left,right in [('destination',observed[:2],reference[:2]),('integer',observed[2],reference[2]),('mxcsr',observed[3],reference[3]),('eflags',observed[4],reference[4])] if left!=right]
 for label in diff:r['differences'][label]+=1
 if diff and len(r['examples'])<12:r['examples'].append({'input':[hex(x) for x in case],'unicorn':[hex(x) for x in observed],'rosetta':[hex(x) for x in reference],'differences':diff})
result={'state':'complete','cases':len(cases),'families':report,'source_sha256':sha(source),'driver_sha256':sha(pathlib.Path(__file__)),'binary_sha256':sha(binary),'request_sha256':sha(out/'requests.txt'),'rosetta_results_sha256':sha(out/'rosetta-results.txt'),'unicorn_version':unicorn.__version__,'scope':'Offline masked-exception register forms; Rosetta versus Unicorn, not physical Intel or native ARM64 equivalence','limits':['Unmasked exception delivery is not tested','Memory operands and DLL execution are not tested','Agreement between references alone does not prove Intel correctness'],'intel_reference':'https://www.intel.com/content/www/us/en/developer/articles/technical/floating-point-reference-sheet-for-intel-architecture.html'}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'cases':len(cases),'families':{name:r['differences'] for name,r in report.items()}},indent=2))
