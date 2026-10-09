#!/usr/bin/env python3
"""Compare the finite provider to its frozen policy on all three Apple routes.
Host component test with controlled context/module services, not a GL test.
"""
import hashlib,json,pathlib,subprocess
from graphics_backend_inputs import prepare,EXPORTS
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/graphics-lookup';out.mkdir(parents=True,exist_ok=True)
prepare(out)
(out/'graphics-test-functions.inc').write_text(''.join('void tbimp_%s(void){}\n'%row[0] for row in EXPORTS))
(out/'fallback.c').write_text('''#include <stdint.h>
unsigned fallback_calls;uint32_t fallback_address;void *fallback_result;
void *tribes_openal_native_lookup(uint32_t address){fallback_calls++;fallback_address=address;return fallback_result;}
''')
# Compile the same production policy branches as each target, running the
# provider-only fixture on macOS. No UIKit or simulated GL backend is involved.
(out/'TargetConditionals.h').write_text('#include_next <TargetConditionals.h>\n#undef TARGET_OS_IPHONE\n#define TARGET_OS_IPHONE TEST_IPHONE\n')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
headers=R/'ref/sdl3/SDL3-3.2.18/include'
assert (headers/'SDL3/SDL.h').exists(),headers
runs=[]
for name,flags in [('macos',[]),('gles',['-DTEST_IPHONE=1']),('gl4es',['-DTEST_IPHONE=1','-DTB_GL4ES'])]:
 executable=out/name
 subprocess.run(['clang','-O2','-g','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-isysroot',sdk,'-I'+str(out),'-I'+str(headers),*flags,str(R/'tests/graphics_lookup_test.c'),str(out/'fallback.c'),'-o',str(executable)],check=True)
 result=subprocess.check_output([str(executable)],text=True);print(name+': '+result,end='');runs.append(dict(platform=name,output=result))
paths=['port/core/guest_graphics.c','scripts/graphics_backend_inputs.py','tests/graphics_lookup_test.c','tests/fixtures/graphics_availability_reference.h','scripts/test-graphics-lookup.py']
(out/'result.json').write_text(json.dumps(dict(passed=True,scope='ASan/UBSan native provider component; mocked context/module services',runs=runs,inputs={p:hashlib.sha256((R/p).read_bytes()).hexdigest() for p in paths}),indent=2)+'\n')
