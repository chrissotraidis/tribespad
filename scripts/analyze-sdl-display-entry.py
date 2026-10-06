#!/usr/bin/env python3
"""Validate the next actual original SDL display API entry without returning it."""
import hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-display-entry';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();cap=json.loads((out/'capture.json').read_text());assert cap['symbol']=='tbimp_SDL_GetPrimaryDisplay' and cap['return_pc']==0x1005b541
cpu=(out/'cpu.bin').read_bytes();assert len(cpu)==440 and sha(out/'cpu.bin')==cap['cpu_sha256'];files=[out/'capture.json',out/'cpu.bin',out/'capture_display.py',out/'run_capture.py',out/'display-lldb.log'];pages={}
for row in cap['ranges']:
 p=out/row['file'];assert sha(p)==row['sha256'];pages[row['address']]=p.read_bytes();files.append(p)
def read(a,n):return pages[a&~4095][a&4095:(a&4095)+n]
sp=struct.unpack_from('<I',cpu,16)[0];assert struct.unpack('<I',read(sp,4))[0]==0x1005b541
assert 'Published post-video tail after three exact stores' in (out/'display-lldb.log').read_text()
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((native/'result.json').read_text());assert d['binary_sha256']==sha(native/'bootstrap');assert 'SDL_GetPrimaryDisplay' in json.dumps(d['result']) or 'SDL_GetPrimaryDisplay' in json.dumps(d.get('stopped_import',{}))
files += [native/'result.json',R/'port/core/guest_sdl_backend.c',R/'port/core/guest_sdl_backend.h',R/'ref/sdl3/SDL3-3.2.18/include/SDL3/SDL_video.h',pathlib.Path(__file__).resolve()]
report=dict(passed=True,binary_sha256=d['binary_sha256'],entry_import='SDL_GetPrimaryDisplay',return_pc=0x1005b541,arguments=[],cpu_bytes=440,bank_slot_values={hex(a):hex(struct.unpack('<I',read(a,4))[0]) for a in [0x101d66d0,0x101d66d4]},sources_sha256={str(p.relative_to(R)):sha(p) for p in files},scope='Actual original main reaches SDL_GetPrimaryDisplay after tail publication and intervening registrations. No primary display ID or desktop mode was returned. Bank words are observed values, not hook-publication acceptance. Next implement native main-thread display APIs and a checked32-bit public mode layout; no window/rendering/gameplay claim.')
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: actual original primary-display import, no result substituted')
