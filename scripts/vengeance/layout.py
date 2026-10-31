#!/usr/bin/env python3
"""Freeze and check the Tribes: Vengeance module layout (plan step V1).

Reads the 27 client and UCC modules from ref/vengeance/game/Program/Bin
(scripts/vengeance/prepare.py), checks them against the October audit,
assigns guest addresses by the plan's rule and checks the result:

- the two executables keep their preferred bases; DLLs are placed in name
  order from 0x11000000 on 64 KiB boundaries;
- no image overlaps another image or a runtime reservation (null page, main
  stack and process metadata below 0x400000, as in the Starsiege harness;
  everything stays below the patch-capture window at 0x2f000000);
- every moved image has relocations, and only HIGHLOW/ABSOLUTE entries;
- every import between the game's own modules names an existing export;
- Windows imports are compared with TribesPad's providers
  (port/llasm-runtime/win32-bindings.json plus Starsiege's additions), giving
  the work list for the UCC closure and for the client.

Writes config/vengeance/layout.json (the frozen map; later modules are
appended, never moved) and docs/validation/vengeance-layout-<date>.json.
Nothing is executed or translated.
"""
import datetime, hashlib, json, pathlib, sys
import pefile

R = pathlib.Path(__file__).resolve().parents[2]
BIN = R / 'ref/vengeance/game/Program/Bin'
AUDIT = R / 'docs/validation/vengeance-planning-audit-2026-10-29.json'
LAYOUT = R / 'config/vengeance/layout.json'
DLL_START, ALIGN, CAPTURE = 0x11000000, 0x10000, 0x2f000000
RESERVED = [(0x0, 0x10000, 'null page'), (0x100000, 0x300000, 'main thread stack'),
            (0x3a0000, 0x3f0000, 'process parameters, PEB, TEB'), (CAPTURE, 0x30000000, 'patch capture window')]
UCC_CLOSURE = ['ucc.exe', 'core.dll', 'msvcr71.dll', 'msvcp71.dll', 'ogg.dll', 'vorbis.dll', 'vorbisfile.dll']
CLIENT = 'tv_cd_dvd.exe'

def up(value, step): return (value + step - 1) // step * step

def main():
    audit = {m['module']: m for m in json.loads(AUDIT.read_text())['candidate_layout']['modules']}
    files = {p.name.lower(): p for p in BIN.iterdir() if p.is_file()}
    missing = sorted(set(audit) - set(files))
    if missing:
        sys.exit('missing modules (run scripts/vengeance/prepare.py): %s' % missing)
    pes, info = {}, {}
    for name in sorted(audit):
        raw = files[name].read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        if digest != audit[name]['sha256']:
            sys.exit('%s differs from the audit' % name)
        pe = pefile.PE(data=raw)
        pes[name] = pe
        kinds = sorted({e.type for b in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', []) for e in b.entries})
        info[name] = {'file': files[name].relative_to(R / 'ref/vengeance/game').as_posix(), 'sha256': digest,
                      'preferred_base': pe.OPTIONAL_HEADER.ImageBase, 'image_size': pe.OPTIONAL_HEADER.SizeOfImage,
                      'relocations': sum(e.type != 0 for b in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', []) for e in b.entries),
                      'relocation_types': kinds, 'tls': hasattr(pe, 'DIRECTORY_ENTRY_TLS'),
                      'stack_reserve': pe.OPTIONAL_HEADER.SizeOfStackReserve, 'exe': not pe.is_dll()}
    # The plan's rule, unless a frozen layout already exists: then keep its bases.
    frozen = json.loads(LAYOUT.read_text())['modules'] if LAYOUT.exists() else {}
    cursor = max([DLL_START] + [m['base'] + m['image_size'] for m in frozen.values()]) if frozen else DLL_START
    for name in sorted(info):
        m = info[name]
        if name in frozen:
            if frozen[name]['sha256'] != m['sha256']:
                sys.exit('%s changed since the layout was frozen' % name)
            m['base'] = frozen[name]['base']
        elif m['exe']:
            m['base'] = m['preferred_base']
        else:
            m['base'] = up(cursor, ALIGN); cursor = m['base'] + m['image_size']
    problems = []
    for name, m in info.items():
        lo, hi = m['base'], m['base'] + m['image_size']
        if lo % ALIGN: problems.append('%s base not 64 KiB aligned' % name)
        if hi > CAPTURE: problems.append('%s ends past 0x%x' % (name, CAPTURE))
        for a, b, what in RESERVED:
            if lo < b and a < hi: problems.append('%s overlaps %s' % (name, what))
        if m['base'] != m['preferred_base'] and not m['relocations']: problems.append('%s moves but has no relocations' % name)
        if set(m['relocation_types']) - {0, 3}: problems.append('%s has relocation types %s' % (name, m['relocation_types']))
    names = sorted(info)
    for i, x in enumerate(names):
        for y in names[i + 1:]:
            if info[x]['exe'] and info[y]['exe']: continue  # one executable per process
            if info[x]['base'] < info[y]['base'] + info[y]['image_size'] and info[y]['base'] < info[x]['base'] + info[x]['image_size']:
                problems.append('%s overlaps %s' % (x, y))
    # Exports by name and ordinal, forwarders noted.
    exports = {}
    for name, pe in pes.items():
        table = getattr(pe, 'DIRECTORY_ENTRY_EXPORT', None)
        byname, byord, forwards = set(), set(), {}
        for s in (table.symbols if table else []):
            byord.add(s.ordinal)
            if s.name: byname.add(s.name.decode())
            if s.forwarder: forwards[(s.name or b'#%d' % s.ordinal).decode()] = s.forwarder.decode()
        exports[name] = (byname, byord, forwards)
    provided = set(json.loads((R / 'port/llasm-runtime/win32-bindings.json').read_text()))
    provided |= set(json.loads((R / 'config/starsiege/bindings.json').read_text())['bindings'])
    internal_missing, forwarders, windows = [], [], {}
    for name, pe in pes.items():
        for table in ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT'):
            for entry in getattr(pe, table, []):
                dll = entry.dll.decode().lower()
                for imp in entry.imports:
                    symbol = imp.name.decode() if imp.name else '#%d' % imp.ordinal
                    if dll in exports:
                        byname, byord, forwards = exports[dll]
                        if not (imp.name and symbol in byname) and not (not imp.name and imp.ordinal in byord):
                            internal_missing.append('%s -> %s!%s' % (name, dll, symbol))
                        if symbol in forwards: forwarders.append('%s!%s -> %s' % (dll, symbol, forwards[symbol]))
                    else:
                        windows.setdefault(dll, {}).setdefault(symbol, set()).add(name)
    def unprovided(modules):
        out = {}
        for dll, symbols in windows.items():
            need = sorted(s for s, users in symbols.items() if users & set(modules) and s not in provided)
            if need: out[dll] = need
        return out
    ucc_need = unprovided(UCC_CLOSURE)
    client_need = unprovided([n for n in info if n != 'ucc.exe'])
    if problems or internal_missing:
        print(json.dumps({'problems': problems, 'internal_imports_missing': internal_missing[:20]}, indent=1))
        sys.exit('layout check failed')
    if not frozen:
        LAYOUT.write_text(json.dumps({'version': 1, 'note': 'Frozen guest layout for Tribes: Vengeance (scripts/vengeance/layout.py). '
            'Append new modules after the last one; never move an existing entry.', 'reserved': [[hex(a), hex(b), w] for a, b, w in RESERVED],
            'modules': {n: {'file': m['file'], 'sha256': m['sha256'], 'base': m['base'], 'image_size': m['image_size']} for n, m in sorted(info.items())}},
            indent=1) + '\n')
    layout_sha = hashlib.sha256(LAYOUT.read_bytes()).hexdigest()
    end = max(m['base'] + m['image_size'] for m in info.values() if not m['exe'])
    today = datetime.date.today().isoformat()
    report = {'date': today, 'layout': 'config/vengeance/layout.json', 'layout_sha256': layout_sha,
              'method': 'pefile headers, relocations, imports and exports of the prepared modules; nothing executed or translated',
              'modules': len(info), 'dll_window': [hex(DLL_START), hex(up(end, ALIGN))],
              'checks_passed': ['hashes match the October audit', 'bases 64 KiB aligned', 'no overlap between images or with reservations',
                                'every moved image has relocations', 'relocation types only ABSOLUTE and HIGHLOW',
                                'every import between game modules resolves by name or ordinal'],
              'executables': {n: {'base': hex(m['base']), 'stack_reserve': m['stack_reserve'], 'tls': m['tls']} for n, m in info.items() if m['exe']},
              'tls_modules': sorted(n for n, m in info.items() if m['tls']),
              'forwarders': forwarders,
              'windows_imports_total': sum(len(s) for s in windows.values()),
              'unprovided_for_ucc_closure': ucc_need, 'unprovided_for_ucc_closure_count': sum(map(len, ucc_need.values())),
              'unprovided_for_client_count': {d: len(s) for d, s in sorted(client_need.items())},
              'still_open': ['Dynamic LoadLibrary/GetProcAddress targets and Unreal package-native pairs (traced at V2)',
                             'Absolute references outside relocation tables (checked by the lifter audit)',
                             'DLL initialization order and CRT ownership across MSVCR71/MSVCP71']}
    out = R / ('docs/validation/vengeance-layout-%s.json' % today)
    out.write_text(json.dumps(report, indent=1) + '\n')
    print('layout ok: %d modules, DLLs %s-%s; UCC closure needs %d Windows functions TribesPad lacks; wrote %s'
          % (len(info), hex(DLL_START), hex(up(end, ALIGN)), report['unprovided_for_ucc_closure_count'], out.relative_to(R)))

if __name__ == '__main__':
    main()
