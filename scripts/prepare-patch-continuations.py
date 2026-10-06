#!/usr/bin/env python3
"""Expose original-image continuation blocks required by the captured bank.

These supplementary blocks retain original guest addresses and stop at existing
native block boundaries. They do not publish patches or replace engine entries.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import shutil
import subprocess

import pefile

R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()


def procedures(code):
    return {m[1]: m[0] for m in re.finditer(
        r'^proc (\S+)[^\n]*\n(?:(?!^proc ).)*?^endp\s*$', code, re.M | re.S)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('engine', type=pathlib.Path)
    p.add_argument('qol', type=pathlib.Path)
    a = p.parse_args()
    bank = R / 'generated/integration-review/native-trampolines'
    meta = json.loads((bank / 'result.json').read_text())
    assert sha(bank / 'module.o') == meta['object_sha256']
    plan_path = R / meta['plan']
    assert sha(plan_path) == meta['plan_sha256']
    plan = json.loads(plan_path.read_text())
    reference = R / plan['reference']
    assert sha(reference) == plan['reference_sha256']
    identities = json.loads(reference.read_text())['inputs_sha256']
    out = R / 'generated/integration-review/native-continuations'
    out.mkdir(exist_ok=True)
    report = dict(state='preparing', published=False, modules={},
                  bank_metadata_sha256=sha(bank / 'result.json'),
                  script_sha256=sha(pathlib.Path(__file__).resolve()))
    report_path = out / 'result.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    srw = R / 'generated/tools/SRW-tribes/SRW.exe'
    build = json.loads(srw.with_name('tribes-build.json').read_text())
    assert sha(srw) == meta['srw_sha256'] == build['srw_sha256']
    assert sha(R / 'scripts/patches/srw-tribes.patch') == build['patch_sha256']
    spec = importlib.util.spec_from_file_location('va', R / 'scripts/prepare-va-model.py')
    va = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(va)
    all_entries = []
    for owner, source, filename in [('engine', a.engine.resolve(), 'Tribes2.exe'),
                                    ('qol', a.qol.resolve(), 'IFC22.dll')]:
        identity = json.loads((source / 'result.json').read_text())
        assert identity['exit'] == 0 and identity['full_flags']
        assert identity['diagnostic'] == (owner == 'qol')
        assert sha(source / filename) == identity['image_sha256'] == identities[owner]
        assert identity['srw_sha256'] == sha(srw)
        pe = pefile.PE(str(source / filename))
        base = pe.OPTIONAL_HEADER.ImageBase
        targets = {v for v in meta['external_targets'] if base <= v < base + pe.OPTIONAL_HEADER.SizeOfImage}
        exports = {e.name.decode(): 'loc_%X' % (base + e.address)
                   for e in getattr(pe, 'DIRECTORY_ENTRY_EXPORT', type('E', (), {'symbols': []})).symbols
                   if e.name and not e.forwarder}
        original = va.replace_names((source / 'seg01_code.llinc').read_text(), exports)
        original_names = set(procedures(original))
        missing = targets - {int(n[4:], 16) for n in original_names if re.fullmatch('loc_[0-9A-F]+', n)}
        work = out / owner
        work.mkdir(exist_ok=True)
        # Only reviewed input files are copied; never inherit generated output.
        copied = [source / filename, source / 'relocations.csv', *source.glob('*.sci')]
        if (source / 'literal_fixups.csv').exists(): copied.append(source / 'literal_fixups.csv')
        assert not (source / 'global_aliases.sci').exists()
        for f in copied: shutil.copy2(f, work / f.name)
        (work / 'global_aliases.sci').write_text(''.join('loc_%X,loc_%X\n' % (v, v) for v in sorted(missing)))
        env = {k: v for k, v in os.environ.items() if k != 'HALOPAD_SRW_DIAG'}
        env['TRIBES_SRW_FULL_FLAGS'] = '1'
        if owner == 'qol': env['HALOPAD_SRW_DIAG'] = '1'
        with (work / 'srw.log').open('w') as log:
            subprocess.run([str(srw), filename, 'image.llasm'], cwd=work, env=env,
                           stdout=log, stderr=log, check=True, timeout=600)
        code = va.replace_names((work / 'seg01_code.llinc').read_text(), exports)
        blocks = procedures(code)
        added = set(blocks) - original_names
        assert {'loc_%X' % v for v in missing} <= added, (owner, missing, added)
        selected = '\n\n'.join(blocks[n] for n in sorted(added)) + '\n'
        assert 'halopad_trap_unimplemented' not in selected, 'Continuation includes an untranslated instruction'
        # No imported calls occur in these pinned prefixes. Future additions
        # need qualified import routing, not an unverified symbol substitution.
        refs = set(re.findall(r'^\s*(?:tcall|ctcallz|ctcallnz)\s+.*?\b(loc_[0-9A-F]+)\s*$', selected, re.M))
        external = refs - added
        assert external <= original_names, external - original_names
        transformed, stats = va.transform(selected.splitlines())
        (work / 'code.llinc').write_text('\n'.join(transformed) + '\n')
        (work / 'extern.llinc').write_text(''.join('proc %s external\n' % n for n in sorted(external)) +
            'proc tribes_dispatch external\nfuncv tribes_set_dispatch_target $, target\n')
        (work / 'module.llasm').write_text('include llasm.llinc\ninclude tribes_mmx.llinc\ninclude tribes_sse.llinc\ninclude tribes_scan.llinc\ninclude tribes_x87.llinc\ninclude tribes_shift.llinc\ninclude tribes_atomic.llinc\ninclude extern.llinc\ninclude code.llinc\n')
        with (work / 'llasm.log').open('w') as log:
            subprocess.run([str(R / 'generated/tools/llasm/llasm'), '-m64', '-ptrofs', '-O2',
                            '-I', str(R / 'port/llasm-support'), '-I', str(R / 'ref/halopad/port/llasm-support'),
                            '-o', str(work / 'raw.ll'), str(work / 'module.llasm')],
                           cwd=work, stdout=log, stderr=log, check=True)
        emitted, removed = va.postprocess(work / 'raw.ll', work / 'module.ll')
        assert set(emitted) == added
        subprocess.run(['clang', '-target', 'arm64-apple-macos12', '-O2', '-Wno-override-module',
                        '-c', str(work / 'module.ll'), '-o', str(work / 'module.o')], check=True)
        undefined = {line.split()[-1] for line in subprocess.check_output(['nm', '-u', str(work / 'module.o')], text=True).splitlines() if line.strip()}
        helpers = {'_tb_x87_fclex'} if owner == 'engine' else set()
        assert undefined <= {'_' + n for n in external} | {'_tribes_dispatch', '_tribes_set_dispatch_target'} | helpers, undefined
        all_entries += [dict(address=int(n[4:], 16), symbol=n, owner=owner) for n in sorted(added)]
        report['modules'][owner] = dict(targets=sorted(targets), new_entries=sorted(added),
            existing_targets=sorted(targets - missing), external_targets=sorted(external),
            undefined_symbols=sorted(undefined), address_transform=stats,
            helper_sources_sha256={str(f.relative_to(R)): sha(f) for f in [R / 'port/llasm-support/tribes_x87.c']} if helpers else {},
            copied_inputs_sha256={str(f.relative_to(R)): sha(f) for f in copied},
            baseline_code_sha256=sha(source / 'seg01_code.llinc'),
            lifted_code_sha256=sha(work / 'seg01_code.llinc'),
            module_sha256=sha(work / 'module.ll'), object_sha256=sha(work / 'module.o'))
        report_path.write_text(json.dumps(report, indent=2) + '\n')
    assert set().union(*(set(m['targets']) for m in report['modules'].values())) == set(meta['external_targets'])
    bridges = []
    for v in meta['external_targets']:
        bridges += ['declare fastcc void @loc_%X(ptr)' % v,
                    'define hidden fastcc void @tbpatch_target_%08x(ptr %%cpu) {\n musttail call fastcc void @loc_%X(ptr %%cpu)\n ret void\n}' % (v, v)]
    (out / 'bridges.ll').write_text('\n'.join(bridges) + '\n')
    (out / 'dispatch-entries.json').write_text(json.dumps(sorted(all_entries, key=lambda r: r['address']), indent=2) + '\n')
    report.update(state='compiled_unpublished', bridges_sha256=sha(out / 'bridges.ll'),
                  dispatch_entries_sha256=sha(out / 'dispatch-entries.json'),
                  scope='Supplementary original continuation blocks and finite target bridges; original downstream dependencies remain external. No patched entry routing, checked link, publication or runtime equivalence claim.')
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
