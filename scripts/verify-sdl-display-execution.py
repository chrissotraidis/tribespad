#!/usr/bin/env python3
"""Compare actual guest display returns with their native SDL values and ABI."""
import hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-display/execution';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();d=json.loads((out/'execution.json').read_text());assert set(d['guest'])=={'before','primary','before_mode','mode'} and set(d['native'])=={'SDL_GetPrimaryDisplay','SDL_GetDesktopDisplayMode'}
files=[out/'execution.json',out/'capture_display.py',out/'run_capture.py',out/'display-lldb.log'];cpu={}
for name,row in d['guest'].items():
 p=out/row['cpu_file'];assert sha(p)==row['cpu_sha256'];cpu[name]=p.read_bytes();assert len(cpu[name])==440;files.append(p)
primary=d['native']['SDL_GetPrimaryDisplay']['value'];assert 0<primary<=0xffffffff and d['guest']['primary']['eax']==primary
assert d['guest']['before_mode']['display_argument']==d['native']['SDL_GetDesktopDisplayMode']['input']==primary
native=d['native']['SDL_GetDesktopDisplayMode'];np=out/native['mode_file'];gp=out/d['guest']['mode']['mode_file'];assert sha(np)==native['mode_sha256'] and sha(gp)==d['guest']['mode']['mode_sha256'];files += [np,gp];nb=np.read_bytes();gb=gp.read_bytes();assert len(nb)==40 and len(gb)==36 and nb[:32]==gb[:32]
assert (struct.unpack_from('<I',gb,32)[0]!=0)==(struct.unpack_from('<Q',nb,32)[0]!=0)
for before,after,value in [('before','primary',primary),('before_mode','mode',d['guest']['mode']['eax'])]:
 expected=bytearray(cpu[before]);struct.pack_into('<I',expected,0,value);struct.pack_into('<I',expected,16,struct.unpack_from('<I',expected,16)[0]+4);assert expected==cpu[after],(before,after)
n=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((n/'result.json').read_text());assert run['binary_sha256']==sha(n/'bootstrap');files += [n/'result.json',pathlib.Path(__file__).resolve()]
report=dict(passed=True,binary_sha256=run['binary_sha256'],display_id=primary,mode_pointer=d['guest']['mode']['eax'],width=struct.unpack_from('<i',gb,8)[0],height=struct.unpack_from('<i',gb,12)[0],native_public_bytes_equal=32,cpu_bytes_per_call=440,sources_sha256={str(p.relative_to(R)):sha(p) for p in files},scope='Actual original zero-argument primary query and one-argument desktop query return real SDL ID/public mode fields. Both complete440-byte CPUs obey cdecl EAX/ESP-only results; private mode pointer becomes an opaque guest token. Debugger endpoint evidence, not whole-memory/timing, window, HD or gameplay acceptance.')
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: original display ID',primary,'mode',report['width'],report['height'],'native/public fields and both CPU returns')
