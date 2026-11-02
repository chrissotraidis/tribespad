#!/usr/bin/env python3
"""Prepare Tribes: Vengeance for TribesPad from the community download.

Downloads the Revengeance community ZIP (or imports a copy you already have
with --archive), checks its SHA-256 against config/vengeance/inputs.json,
unpacks the Inno Setup installer inside it with innoextract (the installer
itself never runs) and writes the game to ref/vengeance/game, keeping its
Program and Content folders. Every file is checked against the recorded
digests. Nothing from the game is stored in this repository. Tribes 2 and
Starsiege: Tribes inputs are separate and unaffected.

Usage: scripts/vengeance/prepare.py [--archive ZIP] [--out ref/vengeance/game] [--cache ref/downloads]
Maintainers: --record rewrites the file digests in config/vengeance/inputs.json
from a verified archive.
"""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess, sys, tempfile, time, zipfile

R = pathlib.Path(__file__).resolve().parents[2]
CONFIG = R / 'config/vengeance/inputs.json'
INPUTS = json.loads(CONFIG.read_text())
MARKER = '.tribespad-inputs.json'

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 22), b''):
            h.update(block)
    return h.hexdigest()

def say(message):
    print('vengeance inputs: ' + message, flush=True)

def archive_path(cache, local):
    """The verified community ZIP in the cache, imported or downloaded."""
    entry = INPUTS['archive']
    target = cache / entry['name']
    if target.exists() and target.stat().st_size == entry['bytes'] and sha256(target) == entry['sha256']:
        say('using ' + str(target))
        return target
    if local:
        say('checking ' + str(local))
        if local.stat().st_size != entry['bytes'] or sha256(local) != entry['sha256']:
            sys.exit('%s is not the recorded archive (expected %d bytes, SHA-256 %s)' % (local, entry['bytes'], entry['sha256']))
        if target.exists():
            target.unlink()
        try:
            os.link(local, target)  # same disk: no second 3 GB copy
        except OSError:
            shutil.copy2(local, target)
        return target
    say('downloading %s (%.1f GB)' % (entry['url'], entry['bytes'] / 1e9))
    partial = target.with_suffix('.part')
    # 3 GB: resume the partial file up to five times; a stall under 1 KB/s for a minute restarts.
    for attempt in range(1, 6):
        result = subprocess.run(['curl', '--fail', '--location', '--retry', '3', '--retry-all-errors', '--connect-timeout', '30',
                                 '--speed-limit', '1024', '--speed-time', '60', '--continue-at', '-',
                                 '--output', str(partial), entry['url']])
        if result.returncode == 0:
            break
        say('download attempt %d failed (curl %d)%s' % (attempt, result.returncode, '; resuming' if attempt < 5 else ''))
        time.sleep(5)
    else:
        sys.exit('could not download %s. Download it yourself from %s and run again with --archive PATH; its SHA-256 is checked.'
                 % (entry['url'], entry['source_page']))
    digest = sha256(partial)
    if digest != entry['sha256']:
        partial.unlink()
        sys.exit('%s has SHA-256 %s, expected %s' % (entry['name'], digest, entry['sha256']))
    partial.rename(target)
    return target

def unzip(archive, work):
    """The installer's three files, by name only; anything else in the ZIP stops."""
    members = INPUTS['archive']['members']
    with zipfile.ZipFile(archive) as z:
        names = [i.filename for i in z.infolist() if not i.is_dir()]
    if sorted(names) != sorted(members):
        sys.exit('unexpected ZIP contents: %s' % names)
    # Deflate64, which Python's zipfile cannot read; 7-Zip extracts the named files only.
    subprocess.run(['7zz', 'x', '-y', '-o' + str(work), str(archive)] + sorted(members), check=True, stdout=subprocess.DEVNULL)
    for name in members:
        if sha256(work / name) != members[name]:
            sys.exit('%s in the ZIP does not match its recorded SHA-256' % name)

def innoextract_version():
    line = subprocess.run(['innoextract', '--version'], capture_output=True, text=True).stdout.splitlines()
    match = re.match(r'innoextract (\d+)\.(\d+)', line[0] if line else '')
    if not match or (int(match.group(1)), int(match.group(2))) < (1, 9):
        sys.exit('innoextract 1.9 or newer is required (brew install innoextract)')
    return '%s.%s' % match.groups()

def extract(work):
    """Unpack the installer's files into work/out after checking every listed path."""
    listing = subprocess.run(['innoextract', '--list', '--quiet', str(work / 'setup.exe')], capture_output=True, text=True, check=True).stdout
    paths = re.findall(r'^ - "(.+?)"', listing, re.M)
    if not paths:
        sys.exit('innoextract listed no files')
    for p in paths:
        if p.startswith('/') or '\\' in p or '..' in p.split('/'):
            sys.exit('unsafe path in the installer: %r' % p)
    out = work / 'out'
    say('extracting %d entries with innoextract (a few minutes)' % len(paths))
    subprocess.run(['innoextract', '--quiet', '--output-dir', str(out), str(work / 'setup.exe')], check=True, stdout=subprocess.DEVNULL)
    root = (out / INPUTS['root']).resolve()
    if not root.is_dir() or not str(root).startswith(str(out.resolve()) + os.sep):
        sys.exit('the installer has no %s folder' % INPUTS['root'])
    return root

def game_files(root):
    """Relative path -> file for everything the build keeps."""
    excluded = tuple(INPUTS['excluded'])
    found = {}
    for p in root.rglob('*'):
        if p.is_symlink():
            sys.exit('unexpected link in the installer: %s' % p)
        if p.is_file():
            name = p.relative_to(root).as_posix()
            if not name.startswith(excluded):
                found[name] = p
    return found

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--archive', type=pathlib.Path, help='a local copy of %s' % INPUTS['archive']['name'])
    ap.add_argument('--out', type=pathlib.Path, default=R / 'ref/vengeance/game')
    ap.add_argument('--cache', type=pathlib.Path, default=R / 'ref/downloads')
    ap.add_argument('--record', action='store_true', help='rewrite the file digests in config/vengeance/inputs.json')
    a = ap.parse_args()
    for tool in ['curl', '7zz', 'innoextract']:
        if not shutil.which(tool):
            sys.exit('missing %s (install with: brew install %s)' % (tool, {'7zz': 'sevenzip'}.get(tool, tool)))
    version = innoextract_version()
    config_digest = sha256(CONFIG)
    marker = a.out / MARKER
    if not a.record and marker.exists() and json.loads(marker.read_text()).get('inputs_sha256') == config_digest:
        say('already prepared: %s' % a.out)
        return
    a.cache.mkdir(parents=True, exist_ok=True)
    archive = archive_path(a.cache, a.archive.resolve() if a.archive else None)
    with tempfile.TemporaryDirectory(dir=a.cache) as work:
        work = pathlib.Path(work)
        say('unpacking the installer from the ZIP')
        unzip(archive, work)
        root = extract(work)
        actual = game_files(root)
        say('checking %d files' % len(actual))
        digests = {name: sha256(path) for name, path in sorted(actual.items())}
        if a.record:
            INPUTS['extractor'] = {'tool': 'innoextract', 'version': version}
            INPUTS['files'] = len(digests)
            INPUTS['bytes'] = sum(p.stat().st_size for p in actual.values())
            INPUTS['game'] = digests
            CONFIG.write_text(json.dumps(INPUTS, indent=1, ensure_ascii=False) + '\n')
            say('recorded %d files, %.2f GiB' % (len(digests), INPUTS['bytes'] / 2**30))
            config_digest = sha256(CONFIG)
        expected = INPUTS['game']
        missing = sorted(set(expected) - set(digests))
        changed = sorted(k for k in expected if k in digests and digests[k] != expected[k])
        extra = sorted(set(digests) - set(expected))
        if missing or changed or extra:
            sys.exit('the installer does not match config/vengeance/inputs.json: missing %s, changed %s, unexpected %s'
                     % (missing[:5], changed[:5], extra[:5]))
        if a.out.exists():
            shutil.rmtree(a.out)
        for name in sorted(expected):
            target = a.out / name
            target.parent.mkdir(parents=True, exist_ok=True)
            os.replace(actual[name], target)  # same disk as the cache: a move, not a copy
    marker.write_text(json.dumps({'inputs_sha256': config_digest, 'archive_sha256': INPUTS['archive']['sha256'],
                                  'extractor': 'innoextract ' + version, 'files': len(expected)}, indent=1) + '\n')
    say('verified %d files: %s' % (len(expected), a.out))

if __name__ == '__main__':
    main()
