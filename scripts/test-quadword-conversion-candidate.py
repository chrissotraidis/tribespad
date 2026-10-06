#!/usr/bin/env python3
"""One-lane native conversion candidate versus exact rational arithmetic."""
import ctypes,fractions,hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=R/'port/core/quadword_conversion.c';out=R/'generated/sse/quadword-conversion-candidate';out.mkdir(exist_ok=True)
commands=[]
for platform,sdkname,target in [('macos','macosx','arm64-apple-macos12'),('device','iphoneos','arm64-apple-ios16'),('simulator','iphonesimulator','arm64-apple-ios16-simulator')]:
 sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();obj=out/(platform+'.o')
 cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-Wall','-Wextra','-Werror','-c',str(source),'-o',str(obj)]
 subprocess.run(cmd,check=True);commands.append(cmd)
exe=out/'native.dylib';subprocess.run(['clang','-dynamiclib','-target','arm64-apple-macos12','-O2',str(source),'-o',str(exe)],check=True)
lib=ctypes.CDLL(str(exe));fn=lib.tb_quadword_conversion;fn.argtypes=[ctypes.c_uint,ctypes.c_uint64,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint64),ctypes.POINTER(ctypes.c_uint32)];fn.restype=ctypes.c_int
F=fractions.Fraction;rng=random.Random(0x6432);controls=[0x1f80|(rounding<<13)|(daz<<6)|(ftz<<15) for rounding in range(4) for daz in range(2) for ftz in range(2)]
from quadword_conversion_reference import oracle

inputs={}
for single in [True,False]:
 n=23 if single else 52;e=8 if single else 11
 edge={0,1,2**n-1,2**(n-1),2**(n-1)-1}
 values=[(sign*2**(n+e)+exponent*2**n+fraction) for exponent in range(2**e) for fraction in edge for sign in range(2)]
 values+= [rng.getrandbits(32 if single else 64) for _ in range(2048)]
 for op in ([0,1] if single else [2,3]):inputs[op]=values
integers=set([0,1,2**64-1,2**63,2**63-1])
for bit in range(64):
 for delta in [-1,0,1]:integers.add((2**bit+delta)%2**64)
integers.update(rng.getrandbits(64) for _ in range(4096))
for op in [4,5,6]:inputs[op]=sorted(integers)
counts={};digest=hashlib.sha256()
for op,values in inputs.items():
 count=0
 for bits in values:
  for control in controls:
   expected,status=oracle(op,bits,control);result=ctypes.c_uint64(0xa5a5a5a5);flags=ctypes.c_uint32(0xa5a5)
   assert fn(op,bits,control,ctypes.byref(result),ctypes.byref(flags))==1
   assert (result.value,flags.value)==(expected,status),(op,hex(bits),hex(control),hex(result.value),hex(expected),flags.value,status)
   digest.update(('%d %x %x %x %x\n'%(op,bits,control,result.value,flags.value)).encode());count+=1
 counts[op]=count;print('Verified conversion operation',op,count,flush=True)
for op,control in [(7,0x1f80),(0,0x10000)]:
 result=ctypes.c_uint64(123);flags=ctypes.c_uint32(456);assert fn(op,0,control,ctypes.byref(result),ctypes.byref(flags))==0 and result.value==123 and flags.value==456
result=ctypes.c_uint64();flags=ctypes.c_uint32();assert fn(0,0,0x1f80,None,ctypes.byref(flags))==0 and fn(0,0,0x1f80,ctypes.byref(result),None)==0
paths=[R/'scripts/quadword_conversion_reference.py',source,R/'port/core/quadword_conversion.h',pathlib.Path(__file__).resolve()]
report={'pass':True,'cases':sum(counts.values()),'counts_by_operation':counts,'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'binary_sha256':sha(exe),'outcomes_sha256':digest.hexdigest(),'compile_commands':commands,'scope':'Native macOS ARM64 one-lane candidate versus independent exact Python Fraction arithmetic. Float exponent/fraction/sign corpus, random inputs, integer boundaries;4 rounding controls xDAZ xFTZ. Compiles device/simulator only. No packed aggregation/CPU publication/decoder/lowering/simulator execution/canonicalEVEX/Windowsfault/fullmodule proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',report['cases'],'native conversion candidate checks')
