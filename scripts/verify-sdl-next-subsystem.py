#!/usr/bin/env python3
"""Identify the actual next original SDL initialization and its import thunk."""
import hashlib,json,pathlib,struct
import pefile
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-next-subsystem';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();d=json.loads((out/'capture.json').read_text());assert d['function']=='tbimp_SDL_InitSubSystem' and d['flags']==16 and d['return_address']==0x100d3b17 and d['thread']==1
for n,k in [('cpu.bin','cpu_sha256'),('stack.bin','stack_sha256')]:assert sha(out/n)==d[k]
assert struct.unpack_from('<II',(out/'stack.bin').read_bytes())==(0x100d3b17,16)
image=R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll';p=pefile.PE(str(image));assert p.get_data(0xd3b10,7)==bytes.fromhex('6a10e886fcffff') and p.get_data(0xd379d,6)==bytes.fromhex('ff2558941210');found=[i for lib in p.DIRECTORY_ENTRY_IMPORT for i in lib.imports if i.address==0x10129458];assert len(found)==1 and found[0].name==b'SDL_InitSubSystem'
header=R/'ref/sdl3/SDL3-3.2.18/include/SDL3/SDL_init.h';assert '#define SDL_INIT_AUDIO      0x00000010u' in header.read_text()
n=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((n/'result.json').read_text());assert run['binary_sha256']==sha(n/'bootstrap') and run['exit']==-6 and run['result']['reason']=='SDL subsystem initialization flags';assert 'Published display immediates after two native-derived stores' in (out/'subsystem-lldb.log').read_text()
files=[out/f for f in ['capture.json','cpu.bin','stack.bin','capture_subsystem.py','run_capture.py','subsystem-lldb.log']]+[image,header,n/'result.json',pathlib.Path(__file__).resolve()]
report=dict(passed=True,binary_sha256=run['binary_sha256'],subsystem='SDL_INIT_AUDIO',flags=16,call=0x100d3b12,thunk=0x100d379d,iat=0x10129458,return_address=0x100d3b17,sources_sha256={str(p.relative_to(R)):sha(p) for p in files},scope='Actual original main requests AUDIO after both display stores publish. Original PUSH/CALL/thunk/IAT and pinned SDL flag agree. Native audio initialization, device enumeration and playback remain unimplemented/unverified.')
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: actual original SDL_INIT_AUDIO entry,0x100d3b12 ->0x100d379d ->IAT0x10129458')
