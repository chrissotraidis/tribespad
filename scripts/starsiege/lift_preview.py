#!/usr/bin/env python3
"""Lift preview for Starsiege: Tribes 1.41: audit, SRW, llasm and clang -O0.

A translation measurement, not a playable build: nothing is linked or run.
Uses the same pinned tools as Tribes 2 (scripts/audit-code.py, SRW with the
Tribes extensions, llasm) on ref/starsiege/game/Tribes.exe, applies the recorded
corrections in config/starsiege/lift-fixups.json, and writes everything to
generated/starsiege/lift. Prints a JSON summary (also lift/result.json).

Usage: .venv/bin/python scripts/starsiege/lift_preview.py [--image PATH] [--no-compile]
"""
import argparse, hashlib, json, pathlib, re, shutil, struct, subprocess, sys, time

R = pathlib.Path(__file__).resolve().parents[2]
FIXUPS = json.loads((R / 'config/starsiege/lift-fixups.json').read_text())
LOCK = json.loads((R / 'toolchains.lock.json').read_text())
OUT = R / 'generated/starsiege/lift'

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def label(address):
    return 'loc_%X' % int(address, 16)

def sixteen_bit_data(image, base, text, rows):
    """Fixups in data whose words look like two 16-bit values (UTF-16 text,
    16-bit tables) next to more of the same, e.g. 0x00600030. With no
    relocation table, the audit can mistake these for pointers. TribesPad keeps
    the image at its original addresses, so emitting such a word as a number
    leaves its value unchanged; a real code pointer dropped here only loses its
    seed and traps loudly if called."""
    word = lambda a: struct.unpack_from('<I', image, a - base)[0] if 0 <= a - base <= len(image) - 4 else 0
    shape = lambda v: v != 0 and (v & 0xff00ff00) == 0
    return {f for f, _ in rows if not (text[0] <= f < text[1]) and shape(word(f))
            and (shape(word(f - 4)) or shape(word(f + 4)))}

def overflow_after_multiply(image, base, starts):
    """SRW has no flags model for seto. In this image every seto follows an
    unsigned 32-bit mul (MSVC's operator new[] size check), possibly with
    register moves between that write neither EDX nor flags. After mul r/m32,
    OF = (EDX != 0). Returns SRW instruction replacements; refuses any other shape."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decode = lambda a: next(md.disasm(image[a - base:a - base + 16], a), None)
    low = {'al': 'eax', 'cl': 'ecx', 'dl': 'edx', 'bl': 'ebx'}
    out = []
    for i, a in enumerate(starts):
        if image[a - base] != 0x0f or image[a - base + 1] != 0x90:
            continue
        insn = decode(a)
        if not insn or insn.mnemonic != 'seto' or insn.op_str not in low or low[insn.op_str] == 'edx':
            raise SystemExit('seto at %#x has an unexpected form' % a)
        k = i - 1
        while decode(starts[k]).mnemonic == 'mov' and re.fullmatch(r'e(a|b|c|s|d)[xip], e..', decode(starts[k]).op_str) \
                and not decode(starts[k]).op_str.startswith('edx'):
            k -= 1
        prev = decode(starts[k])
        if prev.mnemonic != 'mul' or not (prev.op_str.startswith('e') or prev.op_str.startswith('dword')):
            raise SystemExit('seto at %#x does not follow a 32-bit mul (%s %s)' % (a, prev.mnemonic, prev.op_str))
        reg = low[insn.op_str]
        # llasm: cmovz cond, dst, a, b sets dst = (cond == 0) ? a : b.
        out.append('loc_%X,%d,;seto %s (OF of mul = EDX != 0)|cmovz edx, tmp1, 0, 1|and %s, %s, 0xffffff00|or %s, %s, tmp1'
                   % (a, insn.size, insn.op_str, reg, reg, reg, reg))
    return out

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--image', type=pathlib.Path, default=R / 'ref/starsiege/game/Tribes.exe')
    ap.add_argument('--no-compile', action='store_true', help='stop after SRW')
    a = ap.parse_args()
    if sha256(a.image) != FIXUPS['image_sha256']:
        sys.exit('%s is not the pinned 1.41 Tribes.exe' % a.image)
    srw = R / 'generated/tools/SRW-tribes/SRW.exe'
    build = json.loads((R / 'generated/tools/SRW-tribes/tribes-build.json').read_text())
    assert build['patch_sha256'] == LOCK['tribes_extensions']['patch_sha256'] and sha256(srw) == build['srw_sha256']
    # The tools are built locally by scripts/build-tools.py; record what ran.
    report = {'playable': False, 'image_sha256': FIXUPS['image_sha256'], 'srw_sha256': sha256(srw),
              'llasm_sha256': sha256(R / 'generated/tools/llasm/llasm'),
              'fixups_sha256': sha256(R / 'config/starsiege/lift-fixups.json'), 'stages': {}}

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    image = OUT / 'Tribes141.exe'
    shutil.copy2(a.image, image)
    start = time.monotonic()
    subprocess.run([sys.executable, str(R / 'scripts/audit-code.py'), str(image)], check=True)
    analysis = R / 'generated/analysis/tribes141'
    audit = json.loads((analysis / 'audit.json').read_text())
    report['audit'] = {k: (len(audit[k]) if isinstance(audit[k], list) else audit[k]) for k in
                       ['instructions', 'functions', 'unclassified_bytes', 'bad_decodes', 'overlaps',
                        'relocations_certain', 'relocations_uncertain']}
    report['stages']['audit'] = {'seconds': round(time.monotonic() - start, 1)}

    # SRW inputs with the recorded corrections applied.
    not_code = {int(x['address'], 16) for x in FIXUPS['not_code']}
    code = {int(x['address'], 16) for x in FIXUPS['code']}
    rows = [l for l in (analysis / 'relocations.csv').read_text().splitlines() if l]
    pairs = [(int(l.split(',')[0], 16), int(l.split(',')[1], 16)) for l in rows]
    text = (int(FIXUPS['text'][0], 16), int(FIXUPS['text'][1], 16))
    false_pointers = sixteen_bit_data((analysis / 'image.bin').read_bytes(), 0x400000, text, pairs)
    kept = [l for l, (f, t) in zip(rows, pairs) if t not in not_code and f not in false_pointers]
    (OUT / 'relocations.csv').write_text('\n'.join(kept) + '\n')
    for f in (analysis / 'srw').glob('*'):
        shutil.copy2(f, OUT / f.name)
    as_code = set((OUT / 'fixup_interpret_as_code.sci').read_text().split())
    as_data = set((OUT / 'fixup_do_not_interpret_as_code.sci').read_text().split())
    as_code = (as_code - {'loc_%X' % t for t in not_code}) | {'loc_%X' % t for t in code}
    as_data = (as_data - {'loc_%X' % t for t in code}) | {'loc_%X' % t for t in not_code}
    (OUT / 'fixup_interpret_as_code.sci').write_text(''.join(n + '\n' for n in sorted(as_code)))
    (OUT / 'fixup_do_not_interpret_as_code.sci').write_text(''.join(n + '\n' for n in sorted(as_data)))
    starts = sorted(struct.unpack('<%dI' % (len(raw) // 4), raw)) if (raw := (analysis / 'instructions.u32').read_bytes()) else []
    seto = overflow_after_multiply((analysis / 'image.bin').read_bytes(), 0x400000, starts)
    (OUT / 'instruction_replacements.sci').write_text(''.join(
        '%s,%d,%s\n' % (label(x['address']), x['length'], x['llasm']) for x in FIXUPS['instruction_replacements']) +
        ''.join(line + '\n' for line in seto))
    report['corrections'] = {'relocations_dropped': len(rows) - len(kept), 'sixteen_bit_data': len(false_pointers),
                             'not_code': len(not_code),
                             'code': len(code), 'instruction_replacements': len(FIXUPS['instruction_replacements']),
                             'seto_after_mul': len(seto)}

    start = time.monotonic()
    env = {'HALOPAD_SRW_DIAG': '1', 'TRIBES_SRW_FULL_FLAGS': '1', 'PATH': '/usr/bin:/bin'}
    with (OUT / 'stdout.log').open('w') as out, (OUT / 'stderr.log').open('w') as err:
        code_exit = subprocess.run([str(srw), image.name, 'tribes141.llasm'], cwd=OUT, env=env, stdout=out, stderr=err).returncode
    errors = [l for l in (OUT / 'stderr.log').read_text().splitlines() if l.startswith('Error')]
    report['stages']['srw'] = {'exit': code_exit, 'seconds': round(time.monotonic() - start, 1), 'errors': errors[:5]}
    if code_exit != 0:
        return finish(report)
    body = (OUT / 'tribes141.llasm').read_text()
    includes = ''.join('include %s\n' % n for n in ['tribes_mmx.llinc', 'tribes_sse.llinc', 'tribes_scan.llinc',
                                                   'tribes_x87.llinc', 'tribes_shift.llinc', 'tribes_atomic.llinc'])
    (OUT / 'tribes141.llasm').write_text(includes + body)
    # llasm cannot express a data pointer into the middle of another data item
    # ("daddr (loc_X + (n))"). The image keeps its original addresses, so the
    # same word written as its four bytes has the same value.
    inner = re.compile(r'^daddr \(loc_([0-9A-F]+) \+ \((-?\d+)\)\)$', re.M)
    rewritten = 0
    for path in sorted(OUT.glob('seg*_data.llinc')):
        source = path.read_text()
        result, count = inner.subn(lambda m: '\n'.join('db %d' % b for b in struct.pack('<I', int(m[1], 16) + int(m[2]))), source)
        if count:
            path.write_text(result)
            rewritten += count
    report['corrections']['inner_data_pointers_as_bytes'] = rewritten
    import pefile
    pe = pefile.PE(str(image))
    imports = sorted({i.name.decode() for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name})
    declared = set(re.findall(r'^proc (\S+) external', body, re.M))
    (OUT / 'extern.llinc').write_text(''.join('proc %s external\n' % n for n in imports if n not in declared) +
                                      'funcv halopad_trap_unimplemented address\n')
    (OUT / 'macros.llinc').write_text('')
    traps = sorted({m for m in re.findall(r'call halopad_trap_unimplemented (0x[0-9a-f]+)',
                                         ''.join(p.read_text() for p in OUT.glob('seg*_code.llinc')))})
    report['untranslated'] = traps
    if a.no_compile:
        return finish(report)

    start = time.monotonic()
    llasm = R / 'generated/tools/llasm/llasm'
    with (OUT / 'llasm.log').open('w') as log:
        code_exit = subprocess.run([str(llasm), '-m64', '-ptrofs', '-O2', '-I', str(R / 'port/llasm-support'),
                                    '-I', str(R / 'ref/halopad/port/llasm-support'), '-o', str(OUT / 'raw.ll'),
                                    str(OUT / 'tribes141.llasm')], cwd=OUT, stdout=log, stderr=log).returncode
    report['stages']['llasm'] = {'exit': code_exit, 'seconds': round(time.monotonic() - start, 1)}
    if code_exit != 0:
        report['stages']['llasm']['tail'] = (OUT / 'llasm.log').read_text()[-1500:]
        return finish(report)
    # Quote import names that are not plain LLVM identifiers (as compile-engine-census.py does).
    special = sorted((n for n in imports if not re.fullmatch(r'[-a-zA-Z$._][-a-zA-Z$._0-9]*', n)), key=len, reverse=True)
    raw = (OUT / 'raw.ll').read_text()
    if special:
        pattern = re.compile('@(' + '|'.join(re.escape(n) for n in special) + r')(?![\w$?.@])')
        raw = pattern.sub(lambda m: '@"' + m[1] + '"', raw)
    (OUT / 'engine.ll').write_text(raw)
    (OUT / 'raw.ll').unlink()
    start = time.monotonic()
    with (OUT / 'clang.log').open('w') as log:
        code_exit = subprocess.run(['clang', '-arch', 'arm64', '-O0', '-Wno-override-module', '-c', str(OUT / 'engine.ll'),
                                    '-o', str(OUT / 'engine.o')], stdout=log, stderr=log).returncode
    report['stages']['clang'] = {'exit': code_exit, 'seconds': round(time.monotonic() - start, 1)}
    if code_exit == 0:
        report['object_bytes'] = (OUT / 'engine.o').stat().st_size
        nm = subprocess.run(['nm', '-u', str(OUT / 'engine.o')], capture_output=True, text=True).stdout.split()
        report['unresolved_symbols'] = len(nm)
        (OUT / 'unresolved.txt').write_text('\n'.join(nm) + '\n')
    return finish(report)

def finish(report):
    (OUT / 'result.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report, indent=1))
    return 0 if all(s.get('exit', 0) == 0 for s in report['stages'].values()) else 1

if __name__ == '__main__':
    sys.exit(main())

