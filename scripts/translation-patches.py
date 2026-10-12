#!/usr/bin/env python3
"""Recreate TribesPad's translated game code from the player's own game files.

TribesPad does not ship translated game code. A player's build lifts the
downloaded Tribes 2 and TribesNEXT binaries with the pinned lifter ("bases"),
then applies small binary differences from patches/translation ("apply") to
recreate the exact sources the engine was developed and tested with. Every
base and every result is checked against config/translation-patches.json.

  translation-patches.py bases            lift and prepare the bases
  translation-patches.py apply            recreate the translated sources
  translation-patches.py make AUDIT...    maintainers: record patches from this
                                          workspace (AUDIT: audit-build-inputs logs)
"""
import argparse, hashlib, json, os, pathlib, shutil, subprocess, sys, tempfile

R = pathlib.Path(__file__).resolve().parents[1]
CONFIG = R / 'config/translation-patches.json'
PATCHES = R / 'patches/translation'
BASE = R / 'generated/base'
CODECS = json.loads((R / 'config/runtime/miles-codecs.json').read_text())
# Folders the build recreates itself; never recorded as patch targets.
BUILT = ('generated/ipad-engine', 'generated/bochs-math', 'generated/sdl-backend', 'generated/http-backend',
         'generated/gl4es', 'generated/openal-soft', 'generated/softfloat', 'generated/zlib-ng', 'generated/tools',
         'generated/toolchains', 'generated/base', 'generated/apple-game-profile', 'generated/analysis')
MODULES = {'tribes2': 'ref/control/Tribes2.exe', 'ifc22': 'ref/qol/IFC22.dll', 'mss32': 'ref/qol/Mss32.dll'}
MODULES.update({c['module']: c['path'] for c in CODECS})
LIFT_FLAGS = {'tribes2': ['--full-flags', '--tribes-tools'],
              'ifc22': ['--diagnostic', '--full-flags', '--tribes-tools', '--native-zlib-routes']}

def say(message):
    print('translation: ' + message, flush=True)

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

def python():
    return sys.executable

def base_files():
    files = {'tribes2/engine.ll': 'generated/base/tribes2/va/engine.ll',
             'tribes2/seg01_code.llinc': 'generated/base/tribes2/seg01_code.llinc'}
    for module in MODULES:
        if module != 'tribes2':
            files[module + '/module.ll'] = 'generated/base/%s/native-diagnostic/module.ll' % module
            files[module + '/seg01_code.llinc'] = 'generated/base/%s/seg01_code.llinc' % module
    for module, path in MODULES.items():
        files[module + '/image'] = path
    return files

def bases(args):
    BASE.mkdir(parents=True, exist_ok=True)
    for module, image in MODULES.items():
        dest = BASE / module
        done = dest / 'base.json'
        if done.exists():
            continue
        if dest.exists():
            shutil.rmtree(dest)
        say('analysing and lifting ' + module)
        subprocess.run([python(), str(R / 'scripts/audit-code.py'), str(R / image)], check=True, stdout=subprocess.DEVNULL)
        flags = LIFT_FLAGS.get(module, ['--diagnostic', '--full-flags', '--tribes-tools'])
        out = subprocess.run([python(), str(R / 'scripts/lift-image.py'), '--module', module] + flags,
                             check=True, capture_output=True, text=True).stdout
        run = pathlib.Path(out.strip().splitlines()[0])
        run.rename(dest)
        if module == 'tribes2':
            prepare = [python(), str(R / 'scripts/prepare-va-model.py'), str(dest)]
        elif module == 'ifc22':
            prepare = [python(), str(R / 'scripts/prepare-qol-native-diagnostic.py'), str(dest)]
        else:
            prepare = [python(), str(R / 'scripts/prepare-miles-native-diagnostic.py'), str(dest)]
            if module != 'mss32':
                prepare += ['--codec', module]
        subprocess.run(prepare, check=True, stdout=subprocess.DEVNULL)
        done.write_text(json.dumps({'module': module}) + '\n')
    say('bases ready')

def zstd(args):
    subprocess.run(['zstd', '-q', '-f', '--long=31'] + [str(a) for a in args], check=True)

def apply(args):
    config = json.loads(CONFIG.read_text())
    bases = base_files()
    for name, digest in config['bases'].items():
        actual = sha256(R / bases[name])
        if actual != digest:
            sys.exit('The lifted base %s (%s) differs from the recorded one. Your lifter build or game files differ '
                     'from the tested ones; please report this with your Xcode version.' % (name, bases[name]))
    for row in config['files']:
        target = R / row['path']
        if target.exists() and sha256(target) == row['sha256']:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_name(target.name + '.part')
        zstd(['-d', '--memory=2048MB', '--patch-from=' + str(R / bases[row['base']]), str(PATCHES / row['patch']), '-o', str(partial)])
        if sha256(partial) != row['sha256']:
            partial.unlink()
            sys.exit('Recreated %s does not match its recorded SHA-256' % row['path'])
        partial.rename(target)
    say('recreated %d translated files' % len(config['files']))

def candidates(path):
    name = path.name
    parts = path.as_posix()
    for module in MODULES:
        if module in ('tribes2', 'ifc22'):
            continue
        if ('-' + module + '/') in parts or name == module + '.bin':
            return [module + ('/seg01_code.llinc' if name.endswith('.llinc') else '/image' if name.endswith('.bin') else '/module.ll')]
    if name == 'miles.bin':
        return ['mss32/image']
    if name.endswith('.bin'):
        return ['tribes2/image', 'ifc22/image']
    if name.endswith('.llinc'):
        return ['tribes2/seg01_code.llinc', 'ifc22/seg01_code.llinc']
    if 'engine' in parts:
        return ['tribes2/engine.ll', 'ifc22/module.ll']
    return ['ifc22/module.ll', 'tribes2/engine.ll']

def make(args):
    targets = set()
    for log in args.audit:
        for line in open(log):
            event = json.loads(line)
            if event[0] != 'open' or 'r' not in event[2] and event[2] not in ('r', 'rb'):
                continue
            path = pathlib.Path(os.path.realpath(event[1]))
            try:
                rel = path.relative_to(R).as_posix()
            except ValueError:
                continue
            if rel.startswith('generated/') and not rel.startswith(BUILT) and path.is_file():
                targets.add(rel)
    for extra in args.extra:
        targets.add(extra)
    bases = base_files()
    recorded = {name: sha256(R / path) for name, path in bases.items() if (R / path).exists()}
    if PATCHES.exists():
        shutil.rmtree(PATCHES)
    PATCHES.mkdir(parents=True)
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for rel in sorted(targets):
            best = None
            for base in candidates(pathlib.PurePosixPath(rel)):
                if base not in recorded:
                    continue
                out = pathlib.Path(tmp) / 'patch.zst'
                zstd(['-19', '-T0', '--patch-from=' + str(R / bases[base]), str(R / rel), '-o', str(out)])
                size = out.stat().st_size
                if best is None or size < best[1]:
                    best = (base, size)
                    shutil.copyfile(out, pathlib.Path(tmp) / 'best.zst')
                if size < 4096:
                    break
            patch = hashlib.sha256(rel.encode()).hexdigest()[:16] + '.zst'
            shutil.copyfile(pathlib.Path(tmp) / 'best.zst', PATCHES / patch)
            rows.append(dict(path=rel, sha256=sha256(R / rel), base=best[0], patch=patch))
            say('%8d bytes  %s  (from %s)' % (best[1], rel, best[0]))
    used = sorted({row['base'] for row in rows})
    CONFIG.write_text(json.dumps(dict(
        note='Each file is recreated from the player\'s own lift (bases) with patches/translation; no game data is stored.',
        bases={name: recorded[name] for name in used}, files=rows), indent=1) + '\n')
    total = sum((PATCHES / row['patch']).stat().st_size for row in rows)
    say('%d patches, %d bytes' % (len(rows), total))

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('bases')
    sub.add_parser('apply')
    m = sub.add_parser('make')
    m.add_argument('audit', nargs='+')
    m.add_argument('--extra', action='append', default=[])
    args = parser.parse_args()
    dict(bases=bases, apply=apply, make=make)[args.command](args)

if __name__ == '__main__':
    main()

