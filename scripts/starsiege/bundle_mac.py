#!/usr/bin/env python3
"""Add Starsiege: Tribes to a built TribesPad Mac app (launcher plan stage P2).

Puts the Starsiege engine beside Tribes 2's in Contents/MacOS (StarsiegeEngine)
and its game folder, prepared from the player's own 1.41 download
(scripts/starsiege/prepare-inputs.py), in Contents/Resources/GameData/Starsiege.
The Mac game picker (port/apple/launcher_mac.m) then offers Starsiege and execs
it; ANGLE comes from the app's Contents/Frameworks like Tribes 2's. Nothing from
the game goes into the repository.

Usage: .venv/bin/python scripts/starsiege/bundle_mac.py --app generated/macos-engine-hw/TribesPad.app
Builds the engine first (scripts/starsiege/build_engine.py, -O1) when needed.
"""
import argparse, hashlib, importlib.util, json, os, pathlib, shutil, subprocess, sys

R = pathlib.Path(__file__).resolve().parents[2]
ENGINE = R / 'generated/starsiege/engine'
GAME = R / 'ref/starsiege/game'
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def engine():
    result = ENGINE / 'result.json'
    report = json.loads(result.read_text()) if result.exists() else {}
    exe = ENGINE / 'starsiege-engine'
    if report.get('opt') != '-O1' or report.get('engine_sha256') != (sha(exe) if exe.exists() else None):
        print('bundle: building the Starsiege engine at -O1 (a few minutes)', flush=True)
        subprocess.run([sys.executable, str(R / 'scripts/starsiege/build_engine.py')], check=True,
                       env=dict(os.environ, STARSIEGE_OPT='-O1'), stdout=subprocess.DEVNULL)
        report = json.loads(result.read_text())
    assert report.get('opt') == '-O1' and report.get('engine_sha256') == sha(exe), 'Starsiege engine build failed'
    return exe

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--app', required=True, type=pathlib.Path)
    a = ap.parse_args()
    app = a.app.resolve()
    macos, resources = app / 'Contents/MacOS', app / 'Contents/Resources'
    assert (macos / 'TribesPad').exists() and (macos / 'TribesPadEngine').exists(), 'build the Mac app with the game picker first'
    assert (app / 'Contents/Frameworks/libGLESv2.dylib').exists(), 'the Mac app needs its ANGLE frameworks'
    assert (GAME / 'Tribes.exe').exists(), 'run scripts/starsiege/prepare-inputs.py first'
    exe = engine()
    data = resources / 'GameData/Starsiege'
    data.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, macos / 'StarsiegeEngine')
    shutil.copy2(ENGINE / 'image.bin', data / 'image.bin')
    # The game folder, with TribesPad's defaults (build_engine.tribespad_defaults)
    # and without the installer's stale server state.
    subprocess.run(['rsync', '-a', '--delete', '--exclude', 'temp/serverTempData*.cs', str(GAME) + '/', str(data / 'Game') + '/'], check=True)
    load('build_engine', R / 'scripts/starsiege/build_engine.py').tribespad_defaults(data / 'Game')
    files = sorted(p for p in (data / 'Game').rglob('*') if p.is_file())
    digest = hashlib.sha256()
    for p in files:
        digest.update(str(p.relative_to(data / 'Game')).encode() + b'\0' + str(p.stat().st_size).encode() + b'\0')
    digest.update(sha(data / 'Game/console.cs').encode() + sha(data / 'image.bin').encode())
    (data / 'stamp.txt').write_text(digest.hexdigest())
    subprocess.run(['codesign', '--force', '--sign', '-', str(macos / 'StarsiegeEngine')], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    record = dict(app=str(app), engine_sha256=sha(macos / 'StarsiegeEngine'), image_sha256=sha(data / 'image.bin'),
                  game_files=len(files), stamp=digest.hexdigest(),
                  scope='Starsiege dev build in the Mac app (launcher plan P2). Packaging is not gameplay acceptance.')
    (ENGINE / 'bundle-mac.json').write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps(record, indent=1))
    return 0

if __name__ == '__main__':
    sys.exit(main())
