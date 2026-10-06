"""Diagnostic-only exports that stop at first invocation, without API results.

These exist solely in the bootstrap executable selected with --probe-ntdll.
The production provider profile remains limited to implemented services.
"""
import hashlib, importlib.util, json, pathlib
R=pathlib.Path(__file__).resolve().parents[1]
NAMES=['NtDeviceIoControlFile','NtQueryInformationFile','NtSetInformationFile',
       'NtQueryVolumeInformationFile','NtQueryDirectoryFile',
       'NtQuerySystemInformation','NtQueryInformationProcess']

def prepare(out):
    profile=json.loads((R/'config/runtime/native-providers.json').read_text())
    bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text())
    ntdll=next(p for p in profile['providers'] if p['name']=='ntdll.dll')
    probes=[]
    for i,name in enumerate(NAMES):
        if name in ntdll['exports']:continue
        assert name not in bindings
        address=0xfffbf000+16*i
        profile['exports'].append(dict(name=name,guest_va=address))
        ntdll['exports'].append(name);bindings[name]={}
        probes.append(dict(name=name,address=address,index=i))
    spec=importlib.util.spec_from_file_location('provider_generator',R/'scripts/gen-native-system-providers.py')
    generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
    table=out/'probe-native-providers.inc';table.write_text(generator.generate(profile,bindings))
    original=R/'port/core/native_system_providers.c'
    source=out/'probe-native-providers.c'
    text=original.read_text().replace('"native_system_providers.h"','"'+str(R/'port/core/native_system_providers.h')+'"').replace('"guest_modules.h"','"'+str(R/'port/core/guest_modules.h')+'"').replace('"native_system_providers.inc"','"probe-native-providers.inc"')
    source.write_text('/* DIAGNOSTIC ONLY: unimplemented NTDLL exports terminate on invocation. */\n'+text)
    wrappers=out/'ntdll-probes.ll'
    ir='declare void @qol_ntdll_probe(ptr, i32) noreturn\n'
    for row in probes:
        ir+='define hidden fastcc void @tbimp_%s(ptr %%cpu) {\n call void @qol_ntdll_probe(ptr %%cpu, i32 %d)\n unreachable\n}\n'%(row['name'],row['index'])
    wrappers.write_text(ir)
    return [source,wrappers],dict(probes=probes,scope='Diagnostic lookup/dispatch only: no NT service call returns; production exports unchanged.',inputs_sha256={str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [original,table,source,wrappers,R/'scripts/gen-native-system-providers.py',pathlib.Path(__file__).resolve()]})
