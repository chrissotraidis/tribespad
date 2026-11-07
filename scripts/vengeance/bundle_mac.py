#!/usr/bin/env python3
"""Add Tribes: Vengeance to a built TribesPad Mac app (in development).

Puts the Vengeance engine beside the others in Contents/MacOS (VengeanceEngine),
its module images in Contents/Resources/GameData/Vengeance/images and the game
folder, prepared from the player's own download (scripts/vengeance/prepare.py),
in GameData/Vengeance/Game: the campaign and the multiplayer maps. The Mac
game picker (port/apple/launcher_mac.m) then offers it and execs the engine;
ANGLE comes from the app's Contents/Frameworks like the other engines'. Nothing
from the game goes into the repository.

Usage: .venv/bin/python scripts/vengeance/bundle_mac.py --app generated/macos-engine-hw/TribesPad.app
Builds the engine first (scripts/vengeance/build_engine.py --target client) when needed.
"""
import argparse, hashlib, importlib.util, json, pathlib, subprocess, sys

R = pathlib.Path(__file__).resolve().parents[2]
ENGINE = R / 'generated/vengeance/engine-client'
GAME = R / 'ref/vengeance/game'
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()

def engine():
    result = ENGINE / 'result.json'
    exe = ENGINE / 'vengeance-engine'
    report = json.loads(result.read_text()) if result.exists() else {}
    if not exe.exists() or report.get('engine_sha256') != sha(exe) or report.get('platform', 'macos') != 'macos':
        print('bundle: building the Vengeance engine (several minutes)', flush=True)
        subprocess.run([sys.executable, str(R / 'scripts/vengeance/build_engine.py'), '--target', 'client'], check=True, stdout=subprocess.DEVNULL)
        report = json.loads(result.read_text())
    assert report.get('engine_sha256') == sha(exe), 'Vengeance engine build failed'
    return exe

def load_bots():
    """ref/vengeance/bots, fetched now if needed; None (and a note) when unar or the mirror is missing."""
    spec = importlib.util.spec_from_file_location('bots', R / 'scripts/vengeance/bots.py')
    bots = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bots)
    try:
        return bots.fetch()
    except (SystemExit, Exception) as e:
        print('bundle: no Vengeance bots: %s' % e, flush=True)
        return None

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--app', required=True, type=pathlib.Path)
    a = ap.parse_args()
    app = a.app.resolve()
    macos, data = app / 'Contents/MacOS', app / 'Contents/Resources/GameData/Vengeance'
    assert (macos / 'TribesPad').exists() and (macos / 'TribesPadEngine').exists(), 'build the Mac app with the game picker first'
    assert (app / 'Contents/Frameworks/libGLESv2.dylib').exists(), 'the Mac app needs its ANGLE frameworks'
    assert (GAME / 'Program/Bin/TV_CD_DVD.exe').exists(), 'run scripts/vengeance/prepare.py first'
    exe = engine()
    subprocess.run(['cp', str(exe), str(macos / 'VengeanceEngine')], check=True)
    data.mkdir(parents=True, exist_ok=True)
    subprocess.run(['rsync', '-a', '--delete', str(ENGINE / 'images') + '/', str(data / 'images') + '/'], check=True)
    # The whole game: the campaign and the multiplayer maps a server would otherwise send slowly.
    subprocess.run(['rsync', '-a', '--delete', str(GAME) + '/', str(data / 'Game') + '/'], check=True)
    # Bots (scripts/vengeance/bots.py, the community's TRIBESai): its package and four
    # bot maps beside the game's own, when they could be fetched.
    bots = load_bots()
    if bots:
        subprocess.run(['rsync', '-a', str(bots / 'Content') + '/', str(data / 'Game/Content') + '/'], check=True)
    # The same stamp as the iOS build (scripts/build-ipad-engine.py): the app refreshes
    # its copy of the game folder when it changes (port/apple/vengeance_paths.m).
    game = data / 'Game'
    listing = ''.join('%s %d\n' % (f.relative_to(game), f.stat().st_size) for f in sorted(game.rglob('*'))
                      if f.is_file())
    (data / 'stamp.txt').write_text(hashlib.sha256(listing.encode()).hexdigest()[:16] + '\n')
    subprocess.run(['codesign', '--force', '--sign', '-', str(macos / 'VengeanceEngine')], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    record = dict(app=str(app), engine_sha256=sha(macos / 'VengeanceEngine'), game_files=listing.count('\n'),
                  scope='Tribes: Vengeance in the Mac app (in development). Packaging is not gameplay acceptance.')
    (ENGINE / 'bundle-mac.json').write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps(record, indent=1))
    return 0

if __name__ == '__main__':
    sys.exit(main())
