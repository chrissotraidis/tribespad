#!/usr/bin/env python3
"""Prepare Starsiege: Tribes 1.41 for TribesPad from the community download.

Downloads PlayT1's 1.41 installer (or uses a copy you already have), checks its
SHA-256 against config/starsiege/inputs.json, extracts it with 7-Zip and writes
the game to ref/starsiege/game, checking every file against the recorded
digests. Nothing from the game is stored in this repository. Tribes 2's inputs
(scripts/prepare-game-inputs.py) are separate and unaffected.

Usage: scripts/starsiege/prepare-inputs.py [--out ref/starsiege/game] [--cache ref/downloads]
"""
import argparse, hashlib, json, pathlib, shutil, subprocess, sys, tempfile, time

R = pathlib.Path(__file__).resolve().parents[2]
INPUTS = json.loads((R / 'config/starsiege/inputs.json').read_text())

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

def say(message):
    print('starsiege inputs: ' + message, flush=True)

def download(cache):
    entry = INPUTS['installer']
    target = cache / entry['name']
    if target.exists() and sha256(target) == entry['sha256']:
        say('using ' + str(target))
        return target
    say('downloading ' + entry['url'])
    partial = target.with_suffix('.part')
    # 160 MB from PlayT1's host: on a slow or dropping connection, resume the
    # partial file up to five times; a stall under 1 KB/s for a minute restarts.
    for attempt in range(1, 6):
        result = subprocess.run(['curl', '--fail', '--location', '--retry', '3', '--retry-all-errors', '--connect-timeout', '30',
                                 '--speed-limit', '1024', '--speed-time', '60', '--continue-at', '-',
                                 '--output', str(partial), entry['url']])
        if result.returncode == 0:
            break
        say('download attempt %d failed (curl %d)%s' % (attempt, result.returncode, '; resuming' if attempt < 5 else ''))
        time.sleep(5)
    else:
        sys.exit('could not download %s from %s. Download it yourself (playt1.com links it), put it in %s and build again; '
                 'its SHA-256 is checked.' % (entry['name'], entry['url'], cache))
    digest = sha256(partial)
    if digest != entry['sha256']:
        partial.unlink()
        sys.exit('%s has SHA-256 %s, expected %s' % (entry['name'], digest, entry['sha256']))
    partial.rename(target)
    return target

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=pathlib.Path, default=R / 'ref/starsiege/game')
    ap.add_argument('--cache', type=pathlib.Path, default=R / 'ref/downloads')
    a = ap.parse_args()
    for tool in ['curl', '7zz']:
        if not shutil.which(tool):
            sys.exit('missing %s (install with: brew install %s)' % (tool, {'7zz': 'sevenzip'}.get(tool, tool)))
    a.cache.mkdir(parents=True, exist_ok=True)
    installer = download(a.cache)
    expected = INPUTS['game']
    with tempfile.TemporaryDirectory(dir=a.cache) as work:
        work = pathlib.Path(work)
        say('extracting')
        subprocess.run(['7zz', 'x', '-y', '-o' + str(work), str(installer)], check=True, stdout=subprocess.DEVNULL)
        actual = {str(p.relative_to(work)): p for p in work.rglob('*') if p.is_file()}
        missing = sorted(set(expected) - set(actual))
        changed = sorted(k for k in expected if k in actual and sha256(actual[k]) != expected[k])
        if missing or changed:
            sys.exit('the installer does not match config/starsiege/inputs.json: missing %s, changed %s' % (missing[:5], changed[:5]))
        if a.out.exists():
            shutil.rmtree(a.out)
        for name in sorted(expected):
            target = a.out / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(actual[name], target)
    assert sha256(a.out / 'Tribes.exe') == INPUTS['tribes_exe_sha256']
    say('verified %d files: %s' % (len(expected), a.out))

if __name__ == '__main__':
    main()

