#!/usr/bin/env python3
"""Fetch the pinned third-party sources and tools a TribesPad build needs.

Everything comes from public upstream locations at the revisions recorded in
dependencies.lock.json and is checked before use. No game data is fetched here;
scripts/prepare-game-inputs.py downloads the game.

Usage: scripts/fetch-sources.py
"""
import hashlib, json, pathlib, subprocess, sys, tarfile

R = pathlib.Path(__file__).resolve().parents[1]
LOCK = {d['name']: d for d in json.loads((R / 'dependencies.lock.json').read_text())['sources']}

def say(message):
    print('fetch-sources: ' + message, flush=True)

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

def download(url, target, digest):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and sha256(target) == digest:
        return target
    say('downloading ' + url)
    partial = target.with_name(target.name + '.part')
    subprocess.run(['curl', '--fail', '--location', '--retry', '3', '--silent', '--show-error',
                    '--output', str(partial), url], check=True)
    actual = sha256(partial)
    if actual != digest:
        partial.unlink()
        sys.exit('%s has SHA-256 %s, expected %s' % (url, actual, digest))
    partial.rename(target)
    return target

def safe_extract(tf, parent):
    for member in tf.getmembers():
        target = (parent / member.name).resolve()
        if parent.resolve() != target and parent.resolve() not in target.parents or member.isdev() or member.isfifo():
            sys.exit('unsafe archive entry ' + member.name)
    tf.extractall(parent)

def git_checkout(name):
    pin = LOCK[name]
    dest = R / pin['path']
    if (dest / '.git').exists():
        head = subprocess.run(['git', '-C', str(dest), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
        if head == pin['revision']:
            return dest
        sys.exit('%s is at %s, expected %s; move it aside and rerun' % (dest, head, pin['revision']))
    if dest.exists() and any(dest.iterdir()):
        sys.exit('%s exists without Git metadata; move it aside and rerun' % dest)
    say('fetching %s %s' % (name, pin['revision'][:12]))
    dest.mkdir(parents=True, exist_ok=True)
    git = ['git', '-C', str(dest)]
    subprocess.run(git + ['init', '-q'], check=True)
    subprocess.run(git + ['remote', 'add', 'origin', pin['url']], check=True)
    subprocess.run(git + ['fetch', '-q', '--depth', '1', 'origin', pin['revision']], check=True)
    subprocess.run(git + ['-c', 'advice.detachedHead=false', 'checkout', '-q', 'FETCH_HEAD'], check=True)
    return dest

def sdl3():
    pin = LOCK['sdl3-apple-backend']
    archive = download(pin['url'], R / 'ref/sdl3/SDL3-3.2.18.tar.gz', pin['sha256'])
    tree = R / pin['path']
    if not tree.exists():
        with tarfile.open(archive) as tf:
            safe_extract(tf, tree.parent)
    (R / 'ref/sdl3/source.json').write_text(json.dumps(dict(
        version=pin['revision'], url=pin['url'], archive_path='ref/sdl3/SDL3-3.2.18.tar.gz',
        source_path=pin['path'], sha256=pin['sha256'], bytes=archive.stat().st_size), indent=2) + '\n')

def ldc():
    pin = LOCK['ldc']
    parent = R / 'generated/toolchains'
    base = parent / pin['root']
    if not (base / 'bin/ldc2').exists():
        archive = download(pin['url'], parent / pin['archive'], pin['sha256'])
        say('extracting LDC ' + pin['revision'])
        with tarfile.open(archive) as tf:
            safe_extract(tf, parent)
    return base / 'bin/ldc2'

def main():
    for name in ['sr', 'halopad', 'openal-soft', 'zlib-ng-embedded-reference', 'bochs-extended80-candidate', 'gl4es']:
        git_checkout(name)
    sdl3()
    ldc()
    bochs = LOCK['bochs-extended80-candidate']
    for name, digest in bochs['files'].items():
        if sha256(R / bochs['path'] / name) != digest:
            sys.exit('Bochs source changed: ' + name)
    say('sources ready')

if __name__ == '__main__':
    main()

