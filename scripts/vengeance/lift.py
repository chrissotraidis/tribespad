#!/usr/bin/env python3
"""Lift Tribes: Vengeance modules to llasm (goal loop M1).

For each module: copy it from ref/vengeance/game, rebase the copy to its base in
config/vengeance/layout.json (TribesPad keeps translated images at fixed
addresses, so a DLL is rebased before it is audited and lifted, never after),
check the rebase, run the pinned code audit and SRW with the Tribes extensions,
and apply the same mechanical SRW corrections as Starsiege (high-byte movzx).
Unsupported instructions become explicit traps (HALOPAD_SRW_DIAG). Results go
to generated/vengeance/lift/<module>; nothing is linked or run here.

Usage: .venv/bin/python scripts/vengeance/lift.py [module ...]   (default: the UCC closure)
"""
import hashlib, json, os, pathlib, re, shutil, subprocess, sys, time
import pefile

R = pathlib.Path(__file__).resolve().parents[2]
GAME = R / 'ref/vengeance/game'
LAYOUT = json.loads((R / 'config/vengeance/layout.json').read_text())['modules']
OUT = R / 'generated/vengeance/lift'
UCC = ['msvcr71.dll', 'msvcp71.dll', 'ogg.dll', 'vorbis.dll', 'vorbisfile.dll', 'core.dll', 'ucc.exe']
VERSION = 3  # bump when this script's output changes
FIXUPS = json.loads((R / 'config/vengeance/lift-fixups.json').read_text())
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()

def say(message):
    print('vengeance lift: ' + message, flush=True)

def rebase(name, entry, work):
    """A copy of the module at its layout base, with every relocation checked."""
    source = GAME / entry['file']
    if sha(source) != entry['sha256']:
        sys.exit('%s differs from config/vengeance/layout.json' % source)
    original = pefile.PE(str(source))
    delta = entry['base'] - original.OPTIONAL_HEADER.ImageBase
    # Only the relocation sites and ImageBase change. (pefile's relocate_image
    # also rewrites the import directory, which breaks unbound imports.)
    raw = bytearray(source.read_bytes())
    for block in getattr(original, 'DIRECTORY_ENTRY_BASERELOC', []) if delta else []:
        for e in block.entries:
            if e.type == 0:
                continue
            assert e.type == 3, '%s: relocation type %d' % (name, e.type)
            at = original.get_offset_from_rva(e.rva)
            assert at is not None and at + 4 <= len(raw), '%s: relocation outside the file at rva %#x' % (name, e.rva)
            raw[at:at + 4] = ((int.from_bytes(raw[at:at + 4], 'little') + delta) & 0xffffffff).to_bytes(4, 'little')
    field = original.OPTIONAL_HEADER.get_field_absolute_offset('ImageBase')
    raw[field:field + 4] = entry['base'].to_bytes(4, 'little')
    # Sections recorded as data (config/vengeance/lift-fixups.json): clear their
    # code and execute flags in the copy, so the audit and SRW leave them alone.
    for section in original.sections:
        if section.Name.rstrip(b'\x00').decode() in FIXUPS.get(name, {}).get('data_sections', []):
            at = section.get_field_absolute_offset('Characteristics')
            value = int.from_bytes(raw[at:at + 4], 'little') & ~0x20000020
            raw[at:at + 4] = value.to_bytes(4, 'little')
    stem = 'tv_' + pathlib.Path(name).stem
    image = work / (stem + pathlib.Path(name).suffix)
    image.write_bytes(bytes(raw))
    moved = pefile.PE(str(image))
    assert moved.OPTIONAL_HEADER.ImageBase == entry['base']
    checked = 0
    for block in getattr(original, 'DIRECTORY_ENTRY_BASERELOC', []):
        for e in block.entries:
            if e.type == 0:
                continue
            assert e.type == 3, '%s: relocation type %d' % (name, e.type)
            before, after = original.get_dword_at_rva(e.rva), moved.get_dword_at_rva(e.rva)
            assert (after - before) & 0xffffffff == delta & 0xffffffff, '%s: relocation at rva %#x' % (name, e.rva)
            checked += 1
    return image, stem, delta, checked

def lift(name):
    entry = LAYOUT[name]
    work = OUT / pathlib.Path(name).stem
    srw = R / 'generated/tools/SRW-tribes/SRW.exe'
    key = {'version': VERSION, 'module_sha256': entry['sha256'], 'base': entry['base'], 'srw_sha256': sha(srw),
           'audit_sha256': sha(R / 'scripts/audit-code.py'), 'fixups': FIXUPS.get(name)}
    done = work / 'result.json'
    if done.exists() and json.loads(done.read_text()).get('key') == key and json.loads(done.read_text()).get('srw_exit') == 0:
        say('%s: up to date' % name)
        return json.loads(done.read_text())
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    start = time.monotonic()
    image, stem, delta, checked = rebase(name, entry, work)
    report = {'key': key, 'module': name, 'image': image.name, 'image_sha256': sha(image), 'base': hex(entry['base']),
              'delta': delta, 'relocations_checked': checked, 'playable': False}
    with (work / 'audit.log').open('w') as log:
        subprocess.run([sys.executable, str(R / 'scripts/audit-code.py'), str(image)], check=True, stdout=log, stderr=subprocess.STDOUT)
    analysis = R / 'generated/analysis' / stem
    audit = json.loads((analysis / 'audit.json').read_text())
    report['audit'] = {k: (len(audit[k]) if isinstance(audit[k], list) else audit[k]) for k in
                       ['instructions', 'functions', 'unclassified_bytes', 'bad_decodes', 'overlaps', 'relocations_certain', 'relocations_uncertain']
                       if k in audit}
    shutil.copy2(analysis / 'relocations.csv', work / 'relocations.csv')
    for f in (analysis / 'srw').glob('*'):
        shutil.copy2(f, work / f.name)
    # Exports are entry points: seed them as code (the audit seeds from relocations).
    pe = pefile.PE(str(image))
    exports = getattr(pe, 'DIRECTORY_ENTRY_EXPORT', None)
    code_sections = [(s.VirtualAddress, s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData)) for s in pe.sections
                     if s.Characteristics & 0x20000000]
    seeds = {'loc_%X' % (entry['base'] + s.address) for s in (exports.symbols if exports else []) if not s.forwarder
             and any(a <= s.address < b for a, b in code_sections)}
    seeds.add('loc_%X' % (entry['base'] + pe.OPTIONAL_HEADER.AddressOfEntryPoint))
    as_code = set((work / 'fixup_interpret_as_code.sci').read_text().split()) | seeds
    as_data = set((work / 'fixup_do_not_interpret_as_code.sci').read_text().split()) - seeds
    (work / 'fixup_interpret_as_code.sci').write_text(''.join(n + '\n' for n in sorted(as_code)))
    (work / 'fixup_do_not_interpret_as_code.sci').write_text(''.join(n + '\n' for n in sorted(as_data)))
    report['export_seeds'] = len(seeds)
    replacements = FIXUPS.get(name, {}).get('instruction_replacements', [])
    (work / 'instruction_replacements.sci').write_text(''.join('loc_%X,%d,%s\n' % (int(x['address'], 16), x['length'], x['llasm']) for x in replacements))
    report['instruction_replacements'] = len(replacements)
    say('%s: SRW (base %s)' % (name, hex(entry['base'])))
    env = {'HALOPAD_SRW_DIAG': '1', 'TRIBES_SRW_FULL_FLAGS': '1', 'PATH': '/usr/bin:/bin'}
    llasm = stem + '.llasm'
    with (work / 'stdout.log').open('w') as out, (work / 'stderr.log').open('w') as err:
        report['srw_exit'] = subprocess.run([str(srw), image.name, llasm], cwd=work, env=env, stdout=out, stderr=err).returncode
    errors = [l for l in (work / 'stderr.log').read_text(errors='replace').splitlines() if l.startswith('Error')]
    report['srw_errors'] = errors[:10]
    if report['srw_exit'] == 0:
        body = (work / llasm).read_text()
        includes = ''.join('include %s\n' % n for n in ['tribes_mmx.llinc', 'tribes_sse.llinc', 'tribes_scan.llinc',
                                                       'tribes_x87.llinc', 'tribes_shift.llinc', 'tribes_atomic.llinc'])
        (work / llasm).write_text(includes + body)
        # SRW reads "movzx ax, ah" after clearing ax (see scripts/starsiege/lift_preview.py).
        high = re.compile(r'^;movzx ([abcd])x, \1h\n'
                          r'and (e[abcd]x), \2, 0xffff0000\n'
                          r'lshr (tmp\d+), \2, 8\n'
                          r'and \3, \3, 0xff\n', re.M)
        fixed = 0
        for path in sorted(work.glob('seg*_code.llinc')):
            source = path.read_text()
            result, count = high.subn(lambda m: ';movzx %sx, %sh (source read first)\nlshr %s, %s, 8\nand %s, %s, 0xff\nand %s, %s, 0xffff0000\n'
                                      % (m[1], m[1], m[3], m[2], m[3], m[3], m[2], m[2]), source)
            if count:
                path.write_text(result)
                fixed += count
        report['movzx_high_byte_same_register'] = fixed
        code = ''.join(p.read_text() for p in work.glob('seg*_code.llinc'))
        report['untranslated'] = sorted(set(re.findall(r'call halopad_trap_unimplemented (0x[0-9a-f]+)', code)))
        report['procedures'] = len(re.findall(r'^proc loc_', code, re.M))
    report['seconds'] = round(time.monotonic() - start, 1)
    done.write_text(json.dumps(report, indent=1) + '\n')
    say('%s: SRW exit %s, %s procedures, %d untranslated sites, %.0f s' % (name, report['srw_exit'], report.get('procedures'),
                                                                            len(report.get('untranslated', [])), report['seconds']))
    return report

def main():
    names = [n.lower() for n in sys.argv[1:]] or UCC
    failed = [n for n in names if lift(n)['srw_exit'] != 0]
    if failed:
        sys.exit('SRW failed: %s' % failed)

if __name__ == '__main__':
    main()
