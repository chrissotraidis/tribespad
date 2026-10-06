#!/usr/bin/env python3
"""Verify and build pinned SDL, with a private guest environment cache."""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = ROOT / 'config/runtime/sdl-backend.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['macos', 'device', 'simulator'], default='macos')
    args = parser.parse_args()
    config = json.loads(CONFIG.read_text())
    spec = importlib.util.spec_from_file_location('verified_sources', ROOT / 'scripts/build-http-backend.py')
    source = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(source)
    original, files = source.source(config['source'])
    out = ROOT / 'generated/sdl-backend' / args.platform
    out.mkdir(parents=True, exist_ok=True)
    result = out / 'result.json'
    result.unlink(missing_ok=True)
    tree = out / 'source'
    # Keep upstream pristine; this separately identified tree has two exact edits.
    shutil.copytree(original, tree, dirs_exist_ok=True)
    patches = {}
    for patch in config['patches']:
        patched = tree / patch['file']
        text = patched.read_text()
        assert text.count(patch['before']) == 1
        patched.write_text(text.replace(patch['before'], patch['after']))
        patches[patch['file']] = sha(patched)
    for name, digest in files.items():
        if name not in patches:
            assert sha(tree / name) == digest, name
    assert {str(p.relative_to(tree)) for p in tree.rglob('*') if p.is_file()} == set(files)
    sdk_name = dict(macos='macosx', device='iphoneos', simulator='iphonesimulator')[args.platform]
    sdk = subprocess.check_output(['xcrun', '--sdk', sdk_name, '--show-sdk-path'], text=True).strip()
    compiler = subprocess.check_output(['xcrun', '--sdk', sdk_name, '--find', 'clang'], text=True).strip()
    prefix = out / 'install'
    flags = ['-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
             '-DCMAKE_C_COMPILER=' + compiler, '-DCMAKE_OSX_ARCHITECTURES=arm64',
             '-DCMAKE_OSX_SYSROOT=' + sdk,
             '-DCMAKE_OSX_DEPLOYMENT_TARGET=' + ('12.0' if args.platform == 'macos' else '16.0'),
             '-DSDL_SHARED=OFF', '-DSDL_STATIC=ON', '-DSDL_TEST_LIBRARY=OFF',
             '-DSDL_TESTS=OFF', '-DSDL_EXAMPLES=OFF', '-DSDL_INSTALL=ON',
             '-DSDL_INSTALL_CPACK=OFF', '-DSDL_X11=OFF', '-DSDL_WAYLAND=OFF',
             '-DCMAKE_INSTALL_PREFIX=' + str(prefix)]
    if args.platform != 'macos':
        flags += ['-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY']
    commands = []
    for name, command in [
        ('configure', ['cmake', '--fresh', '-G', 'Ninja', '-S', str(tree), '-B', str(out / 'build'), *flags]),
        ('build', ['cmake', '--build', str(out / 'build'), '--parallel', '4']),
        ('install', ['cmake', '--install', str(out / 'build')]),
    ]:
        commands.append(command)
        with (out / (name + '.log')).open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    library = prefix / 'lib/libSDL3.a'
    architecture = subprocess.check_output(['lipo', '-archs', str(library)], text=True).strip()
    assert architecture == 'arm64', architecture
    headers = {str(p.relative_to(prefix)): sha(p) for p in (prefix / 'include').rglob('*.h')}
    shutil.copyfile(original / 'LICENSE.txt', ROOT / 'port/licenses/SDL3-LICENSE.txt')
    report = dict(passed=True, platform=args.platform, sdk=sdk, commands=commands,
                  archive_sha256=config['source']['sha256'], source_files=files,
                  patches=patches,
                  config_sha256=sha(CONFIG), script_sha256=sha(pathlib.Path(__file__)),
                  libraries={str(library.relative_to(ROOT)): sha(library)},
                  headers=headers, architecture=architecture,
                  pkgconfig=(prefix / 'lib/pkgconfig/sdl3.pc').read_text(),
                  scope=config['scope'], execution_tested=False)
    result.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: static SDL3 ARM64 backend (' + args.platform + '); execution not tested')


if __name__ == '__main__':
    main()
