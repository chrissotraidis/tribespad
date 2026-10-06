#!/usr/bin/env python3
"""Verify real native provider lookup and dynamically dispatched service calls."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/system-providers';out.mkdir(exist_ok=True);(out/'result.json').write_text('{"pass":false,"state":"running"}\n')
b=R/'generated/native-win32/macos';build=json.loads((b/'result.json').read_text())
for name,h in build['system_providers_sha256'].items():assert sha(R/name)==h
profile=json.loads((R/'config/runtime/native-providers.json').read_text());by_name={e['name']:e for e in profile['exports']};header='typedef struct {const char *name;uint32_t token;void *function;} export_case;\ntypedef struct {const char *name;uint32_t image;const export_case *exports;unsigned count,initial_references;} provider_case;\n'
header+=''.join('extern void tbimp_'+n+'(void);\n' for n in by_name)
for i,m in enumerate(profile['providers']):header+='static const export_case exports_%d[]={\n'%i+''.join('{"%s",%du,(void *)tbimp_%s},\n'%(n,by_name[n]['guest_va'],n) for n in m['exports'])+'};\n'
header+='static const provider_case providers[]={\n'+''.join('{"%s",%du,exports_%d,%d,%d},\n'%(m['name'],m['image'],i,len(m['exports']),m['initial_references']) for i,m in enumerate(profile['providers']))+'};\n';(out/'providers.inc').write_text(header)
# Empty image dispatch still uses the actual runtime fallback/native route table.
(out/'empty.ll').write_text('@tribes_dispatch_count = constant i32 0\n@tribes_dispatch_vas = constant [1 x i32] [i32 0]\n@tribes_dispatch_fns = constant [1 x ptr] [ptr null]\n')
objects=[]
for name,row in build['objects'].items():
    obj=b/(name+'.o');assert sha(obj)==row['object_sha256'];objects.append(str(obj))
sources=[R/'port/core/guest_memory_checks.c',R/'port/llasm-support/tribes_absolute_memory.c',R/'tests/native_system_providers_test.c',R/'tests/fixtures/native_enter.ll',out/'empty.ll'];exe=out/'native'
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+list(map(str,sources))+objects+[str(R/'generated/softfloat/macos/softfloat.a'),'-framework','Foundation','-framework','AppKit','-framework','CoreGraphics','-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
(out/'run.log').write_text(run.stdout);(out/'result.json').write_text(json.dumps({'pass':True,'output':run.stdout.strip(),'binary_sha256':sha(exe),'runtime_build_sha256':sha(b/'result.json'),'source_sha256':{str(p.relative_to(R)):sha(p) for p in sources+[pathlib.Path(__file__).resolve(),R/'config/runtime/native-providers.json',R/'port/core/native_system_providers.inc',R/'port/core/native_system_providers.c',R/'port/core/native_exports.c']},'scope':'Finite native provider export lookups, process-start references surviving balanced temporary loads, actual immutable dispatch pointers, DirectInput coexistence and dynamically returned TLS, BCrypt RNG and NT file/volume query ABI execution; no complete Windows loader or game acceptance.'},indent=2)+'\n');print(run.stdout.strip())
