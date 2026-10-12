#!/usr/bin/env python3
"""Prepare TribesPad's game inputs from the official free downloads.

Downloads the Tribes 2 installer and the TribesNEXT client (or uses copies you
already have), checks their SHA-256 against inputs-manifest.json, builds the
pinned REWise extractor, and assembles three folders:

  <out>/control   the original game (GameData from the official installer)
  <out>/qol       the TribesNEXT client files
  <out>/product   control with the TribesNEXT files applied (what TribesPad runs)

Every extracted file is checked against config/game-inputs.json, which records
only paths and SHA-256 digests. No game data is stored in this repository; the
optional HD texture pack is added only when you supply it with --hd-textures.

Usage:
  scripts/prepare-game-inputs.py [--out ref] [--cache DIR] [--hd-textures FILE]
"""
import argparse, hashlib, json, pathlib, shutil, subprocess, sys, tempfile

R = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = json.loads((R / 'inputs-manifest.json').read_text())
EXPECTED = json.loads((R / 'config/game-inputs.json').read_text())
LOCK = {d['name']: d for d in json.loads((R / 'dependencies.lock.json').read_text())['sources']}

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

def say(message):
    print('prepare-game-inputs: ' + message, flush=True)

def download(name, cache):
    entry = next(f for f in MANIFEST['files'] if f['name'] == name)
    target = cache / name
    if target.exists() and sha256(target) == entry['sha256']:
        say('using ' + str(target))
        return target
    say('downloading ' + entry['url'])
    partial = target.with_suffix(target.suffix + '.part')
    # curl: some download hosts refuse Python's default client. Resumes a
    # partial download, retries, and fails on HTTP errors.
    subprocess.run(['curl', '--fail', '--location', '--retry', '3', '--continue-at', '-',
                    '--output', str(partial), entry['url']], check=True)
    digest = sha256(partial)
    if digest != entry['sha256']:
        partial.unlink()
        sys.exit('%s has SHA-256 %s, expected %s' % (name, digest, entry['sha256']))
    partial.rename(target)
    return target

def rewise(cache):
    pin = LOCK['rewise']
    source = cache / ('rewise-' + pin['revision'][:12])
    binary = source / 'build/rewise'
    if binary.exists():
        return binary
    say('building REWise ' + pin['revision'][:12])
    if not source.exists():
        subprocess.run(['git', 'clone', '-q', pin['url'], str(source)], check=True)
    subprocess.run(['git', '-C', str(source), 'checkout', '-q', pin['revision']], check=True)
    # Use the SDK xcrun selects, not a possibly mismatched Command Line Tools SDK.
    sdk = subprocess.run(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], check=True,
                         capture_output=True, text=True).stdout.strip()
    subprocess.run(['cmake', '-S', str(source), '-B', str(source / 'build'), '-DCMAKE_BUILD_TYPE=Release',
                    '-DCMAKE_OSX_SYSROOT=' + sdk],
                   check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(source / 'build')], check=True, stdout=subprocess.DEVNULL)
    return binary

def tree(root):
    return {str(p.relative_to(root)): sha256(p) for p in sorted(root.rglob('*')) if p.is_file()}

def verify(name, root, expected):
    actual = tree(root)
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    changed = sorted(k for k in set(actual) & set(expected) if actual[k] != expected[k])
    if missing or extra or changed:
        sys.exit('%s does not match config/game-inputs.json: missing %s, extra %s, changed %s'
                 % (name, missing[:5], extra[:5], changed[:5]))
    say('%s verified: %d files' % (name, len(actual)))

def replace_dir(path):
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True)

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=pathlib.Path, default=R / 'ref')
    ap.add_argument('--cache', type=pathlib.Path, default=R / 'ref/downloads')
    ap.add_argument('--hd-textures', type=pathlib.Path, help='your copy of yHDTextures2.0.vl2 (optional)')
    a = ap.parse_args()
    a.cache.mkdir(parents=True, exist_ok=True)
    for tool in ['curl', 'git', 'cmake', '7zz']:
        if not shutil.which(tool):
            sys.exit('missing %s (install with: brew install %s)' % (tool, {'7zz': 'sevenzip'}.get(tool, tool)))
    installer = download('tribes2gsi.exe', a.cache)
    client = download(EXPECTED['tribesnext_installer'], a.cache)
    extractor = rewise(a.cache)
    with tempfile.TemporaryDirectory(dir=a.cache) as work:
        work = pathlib.Path(work)
        say('extracting the Tribes 2 installer')
        (work / 'wise').mkdir()
        subprocess.run([str(extractor), '-x', str(work / 'wise'), str(installer)], check=True, stdout=subprocess.DEVNULL)
        control = a.out / 'control'
        replace_dir(control)
        shutil.copytree(work / 'wise/MAINDIR/Tribes2/GameData', control, dirs_exist_ok=True)
        verify('control', control, EXPECTED['control'])
        say('extracting the TribesNEXT client')
        qol = a.out / 'qol'
        replace_dir(qol)
        subprocess.run(['7zz', 'x', '-y', '-o' + str(qol), str(client)], check=True, stdout=subprocess.DEVNULL)
        verify('qol', qol, EXPECTED['qol'])
    product = a.out / 'product'
    replace_dir(product)
    shutil.copytree(control, product, dirs_exist_ok=True)
    for name in EXPECTED['qol']:
        if name in EXPECTED['qol_excluded_from_product']:
            continue
        target = product / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(qol / name, target)
    if a.hd_textures:
        hd = next(f for f in MANIFEST['files'] if f['name'] == 'yHDTextures2.0.vl2')
        if sha256(a.hd_textures) != hd['sha256']:
            sys.exit('the HD texture pack does not match the pinned yHDTextures2.0.vl2')
        shutil.copy2(a.hd_textures, product / 'base/yHDTextures2.0.vl2')
    expected_product = dict(EXPECTED['control'])
    expected_product.update({k: v for k, v in EXPECTED['qol'].items() if k not in EXPECTED['qol_excluded_from_product']})
    if a.hd_textures:
        expected_product['base/yHDTextures2.0.vl2'] = hd['sha256']
    verify('product', product, expected_product)
    for module in MANIFEST['modules']:
        if module['path'] in ('ref/control/Tribes2.exe',):
            assert sha256(a.out / 'control/Tribes2.exe') == module['sha256']
    say('done: ' + str(a.out))

if __name__ == '__main__':
    main()
