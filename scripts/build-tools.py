#!/usr/bin/env python3
"""Build the lifter tools from the pinned sources fetched by fetch-sources.py.

  generated/tools/SRW        SRW (M-HT/SR) with HaloPad's macOS llasm patch
  generated/tools/llasm      llasm, compiled with the pinned LDC
  generated/tools/SRW-tribes SRW with scripts/patches/srw-tribes.patch
  generated/tools/*-pass     TribesPad's LLVM 20 passes (Homebrew llvm@20)

Source identities are checked against toolchains.lock.json. Compiled binaries
can differ between Xcode versions, so tools-build.json records this machine's
binaries; the translation stage later checks the lifter output itself.

Usage: scripts/build-tools.py
"""
import hashlib, json, os, pathlib, shutil, subprocess, sys

R = pathlib.Path(__file__).resolve().parents[1]
LOCK = json.loads((R / 'toolchains.lock.json').read_text())
DEPS = {d['name']: d for d in json.loads((R / 'dependencies.lock.json').read_text())['sources']}
TOOLS = R / 'generated/tools'

def say(message):
    print('build-tools: ' + message, flush=True)

def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def run(args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)

def srw_base():
    dest = TOOLS / 'SRW'
    if (dest / 'SRW.exe').exists() and all(sha256(dest / f) == h for f, h in LOCK['tribes_extensions']['base_sources'].items()):
        return dest
    say('building SRW')
    if dest.exists():
        shutil.rmtree(dest)
    patch = R / 'ref/halopad/port/patches/srw-macos-llasm.patch'
    if sha256(patch) != LOCK['halopad_build_manifest']['identity']['patch']:
        sys.exit('HaloPad SRW patch identity mismatch')
    shutil.copytree(R / 'ref/sr/SRW', dest)
    run(['patch', '--batch', '-p1', '-i', patch], cwd=dest)
    for name, digest in LOCK['tribes_extensions']['base_sources'].items():
        if sha256(dest / name) != digest:
            sys.exit('SRW source mismatch: ' + name)
    run([sys.executable, '-m', 'SCons', '-j', str(os.cpu_count() or 4)], cwd=dest)
    return dest

def llasm():
    dest = TOOLS / 'llasm'
    if (dest / 'llasm').exists():
        return dest
    say('building llasm')
    ldc = R / 'generated/toolchains' / DEPS['ldc']['root'] / 'bin/ldc2'
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(R / 'ref/sr/llasm', dest)
    run([ldc, '-O2', 'llasm.d', '-of=llasm'], cwd=dest)
    return dest

def main():
    import SCons
    if SCons.__version__ != LOCK['tribes_extensions']['scons']:
        sys.exit('SCons %s required' % LOCK['tribes_extensions']['scons'])
    # Build with the selected Xcode's macOS SDK, also inside SCons.
    os.environ.setdefault('SDKROOT', subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip())
    os.environ['SCONSFLAGS'] = '--site-dir=' + str(R / 'scripts/scons_site')
    srw = srw_base()
    tool = llasm()
    tribes = TOOLS / 'SRW-tribes'
    build = tribes / 'tribes-build.json'
    if not (build.exists() and json.loads(build.read_text())['patch_sha256'] == sha256(R / 'scripts/patches/srw-tribes.patch')
            and sha256(tribes / 'SRW.exe') == json.loads(build.read_text())['srw_sha256']):
        say('building the Tribes lifter')
        run([sys.executable, R / 'scripts/build-tribes-lifter.py'])
    for name in ['guest-memory-pass', 'external-memory-pass']:
        if not (TOOLS / name / 'result.json').exists():
            say('building ' + name)
            run([sys.executable, R / ('scripts/build-%s.py' % name)])
    record = {
        'base_sources': LOCK['tribes_extensions']['base_sources'],
        'artifacts': {'SRW/SRW.exe': sha256(srw / 'SRW.exe'), 'llasm/llasm': sha256(tool / 'llasm'),
                      'SRW-tribes/SRW.exe': sha256(tribes / 'SRW.exe')},
        'clang': subprocess.check_output(['clang', '--version'], text=True),
        'sdk': subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-build-version'], text=True).strip(),
    }
    (TOOLS / 'tools-build.json').write_text(json.dumps(record, indent=2) + '\n')
    say('tools ready')

if __name__ == '__main__':
    main()

