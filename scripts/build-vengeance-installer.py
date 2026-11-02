#!/usr/bin/env python3
"""Build the Tribes: Vengeance installer library for one Apple platform.

TribesPad downloads the community release on the player's device and unpacks it
there (port/apple/vengeance_download.m), as scripts/vengeance/prepare.py does on
a Mac. This builds that unpacker: innoextract 1.9 with the parts of Boost it
uses (Filesystem, Iostreams' zlib and bzip2 filters), liblzma from xz and zlib's
Deflate64 decoder (contrib/infback9) for the release's ZIP. Sources are pinned
in config/vengeance/installer.json and checked on download; nothing from them is
committed. The result is generated/vengeance-installer/<platform>/libvginstaller.a.

Usage: .venv/bin/python scripts/build-vengeance-installer.py --platform macos|device|simulator
"""
import argparse, hashlib, json, pathlib, shutil, subprocess, tarfile, urllib.request

R = pathlib.Path(__file__).resolve().parents[1]
PINS = json.loads((R / 'config/vengeance/installer.json').read_text())
SRC = R / 'ref/installer-sources'
TARGETS = {'macos': ('macosx', 'arm64-apple-macos12'), 'device': ('iphoneos', 'arm64-apple-ios16'),
           'simulator': ('iphonesimulator', 'arm64-apple-ios16-simulator')}

def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 22), b''):
            h.update(block)
    return h.hexdigest()

def fetch(pin):
    """The pinned archive, downloaded once and checked, unpacked into ref/installer-sources."""
    archive = SRC / pin['archive']
    SRC.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        print('vengeance installer: downloading ' + pin['url'], flush=True)
        part = archive.with_suffix(archive.suffix + '.download')
        with urllib.request.urlopen(pin['url'], timeout=120) as r, open(part, 'wb') as out:
            shutil.copyfileobj(r, out)
        part.replace(archive)
    if sha(archive) != pin['sha256']:
        raise SystemExit('%s does not match its pinned SHA-256' % archive)
    tree = SRC / pin['tree']
    if not tree.exists():
        with tarfile.open(archive) as t:
            members = [m for m in t.getmembers() if not pin.get('only') or any(m.name.startswith(pin['tree'] + '/' + o) for o in pin['only'])]
            for m in members:
                if m.name.startswith('/') or '..' in pathlib.PurePosixPath(m.name).parts or not (m.isfile() or m.isdir()):
                    raise SystemExit('unexpected archive entry: ' + m.name)
            t.extractall(SRC, members=members)
    return tree

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--platform', choices=sorted(TARGETS), required=True)
    a = ap.parse_args()
    sdk_name, triple = TARGETS[a.platform]
    sdk = subprocess.check_output(['xcrun', '--sdk', sdk_name, '--show-sdk-path'], text=True).strip()
    trees = {name: fetch(pin) for name, pin in PINS['sources'].items()}
    out = R / 'generated/vengeance-installer' / a.platform
    record = out / 'result.json'
    key = hashlib.sha256(json.dumps(PINS, sort_keys=True).encode() + b''.join(
        sha(p).encode() for p in sorted((R / 'port/vengeance/installer').iterdir()) + [pathlib.Path(__file__)])).hexdigest()
    if record.exists() and json.loads(record.read_text()).get('key') == key and (out / 'libvginstaller.a').exists():
        print('vengeance installer: up to date (%s)' % a.platform)
        return
    if out.exists():
        shutil.rmtree(out)
    (out / 'obj').mkdir(parents=True)
    # liblzma through xz's own CMake build.
    lz = out / 'xz'
    system = ['-DCMAKE_SYSTEM_NAME=iOS'] if a.platform != 'macos' else []
    subprocess.run(['cmake', '-S', str(trees['xz']), '-B', str(lz), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF',
                    '-DCMAKE_OSX_SYSROOT=' + sdk, '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=' + ('12.0' if a.platform == 'macos' else '16.0'),
                    '-DXZ_NLS=OFF', '-DXZ_TOOL_XZ=OFF', '-DXZ_TOOL_XZDEC=OFF', '-DXZ_TOOL_LZMADEC=OFF', '-DXZ_TOOL_LZMAINFO=OFF',
                    '-DXZ_TOOL_SCRIPTS=OFF', '-DXZ_DOC=OFF', '-DBUILD_TESTING=OFF'] + system, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(lz), '--target', 'liblzma'], check=True, stdout=subprocess.DEVNULL)
    boost, zlib = trees['boost'], trees['zlib']
    # innoextract 1.9 with the upstream fixes for newer Boost, as Homebrew builds it.
    inno = out / 'innoextract'
    shutil.copytree(trees['innoextract'], inno)
    for patch in PINS['sources']['innoextract'].get('patches', []):
        path = SRC / patch['file']
        if not path.exists():
            with urllib.request.urlopen(patch['url'], timeout=60) as r:
                path.write_bytes(r.read())
        if sha(path) != patch['sha256']:
            raise SystemExit('%s does not match its pinned SHA-256' % path)
        subprocess.run(['patch', '-p1', '-s', '-d', str(inno), '-i', str(path)], check=True)
    gen = out / 'include'
    gen.mkdir()
    # innoextract's CMake-generated headers, for this one configuration.
    config = (inno / 'src/configure.hpp.in').read_text()
    values = {'ISATTY': 1, 'IOCTL': 1, 'TIMEGM': 1, 'GMTIME_R': 1, 'UTIMENSAT': 1, 'DYNAMIC_UTIMENSAT': 0, 'AT_FDCWD': 1, 'UTIMES': 1,
              'DLSYM': 0, 'BUILTIN_BSWAP16': 1, 'BUILTIN_BSWAP32': 1, 'BUILTIN_BSWAP64': 1, 'BSWAP_16': 0, 'BSWAP_32': 0, 'BSWAP_64': 0,
              'ALIGNOF': 1, 'STD_CODECVT_UTF8_UTF16': 0, 'STD_UNIQUE_PTR': 1, 'ARC4': 1, 'LZMA': 1, 'ICONV': 0, 'WIN32_CONV': 0,
              'BUILTIN_CONV': 1, 'POSIX_SPAWNP': 0, 'UNISTD_ENVIRON': 0, 'FORK': 0, 'EXECVP': 0, 'WAITPID': 0}
    for name, v in values.items():
        config = config.replace('#cmakedefine01 INNOEXTRACT_HAVE_' + name + '\n', '#define INNOEXTRACT_HAVE_%s %d\n' % (name, v))
    assert '#cmakedefine' not in config, config
    (gen / 'configure.hpp').write_text(config)
    (gen / 'release.cpp').write_text('#include "release.hpp"\nconst char innoextract_name[]="innoextract";\nconst char innoextract_version[]="1.9";\n'
        'const char innoextract_bugs[]="https://github.com/dscharrer/innoextract/issues";\nconst char innoextract_copyright[]="Copyright (C) 2011-2020 Daniel Scharrer";\n'
        'const char innoextract_license[]="zlib license";\nconst char innoextract_build_date[]="";\nconst char innoextract_build_commit[]="";\n')
    # Its command line (main.cpp), Windows-only code and process launching (vg_process.cpp instead) stay out.
    sources = [p for p in sorted((inno / 'src').rglob('*.cpp')) if p.name not in ('main.cpp', 'windows.cpp', 'process.cpp') or p.parent.name == 'setup'] + [gen / 'release.cpp']
    sources += [boost / 'libs/filesystem/src' / n for n in ['codecvt_error_category.cpp', 'directory.cpp', 'exception.cpp', 'operations.cpp',
                'path.cpp', 'path_traits.cpp', 'portability.cpp', 'unique_path.cpp', 'utf8_codecvt_facet.cpp']]
    sources += [boost / 'libs/iostreams/src' / n for n in ['zlib.cpp', 'bzip2.cpp']]
    sources += sorted((R / 'port/vengeance/installer').glob('*.cpp'))
    csources = [zlib / 'contrib/infback9/infback9.c', zlib / 'contrib/infback9/inftree9.c'] + sorted((R / 'port/vengeance/installer').glob('*.c'))
    common = ['-target', triple, '-isysroot', sdk, '-O2', '-fvisibility=hidden', '-I', str(gen), '-I', str(inno / 'src'), '-isystem', str(boost),
              '-I', str(trees['xz'] / 'src/liblzma/api'), '-DBOOST_ALL_NO_LIB',
              '-DBOOST_FILESYSTEM_NO_CXX20_ATOMIC_REF', '-DLZMA_API_STATIC', '-w']
    objects = []
    for s in sources + csources:
        obj = out / 'obj' / (s.parent.name + '_' + s.stem + '.o')
        # zlib's own source folder, for its Deflate64 decoder only (its headers would shadow the C library's).
        lang = ['-x', 'c++', '-std=c++14'] if s.suffix == '.cpp' else ['-x', 'c', '-std=c11', '-I', str(zlib), '-I', str(zlib / 'contrib/infback9')]
        subprocess.run(['clang'] + common + lang + ['-c', str(s), '-o', str(obj)], check=True)
        objects.append(str(obj))
    lib = out / 'libvginstaller.a'
    subprocess.run(['libtool', '-static', '-o', str(lib)] + objects + [str(lz / 'liblzma.a')], check=True, stderr=subprocess.DEVNULL)
    record.write_text(json.dumps({'key': key, 'platform': a.platform, 'library': str(lib.relative_to(R)), 'sha256': sha(lib),
                                  'link': ['-lz', '-lbz2', '-lc++']}, indent=1) + '\n')
    print('vengeance installer: ' + str(lib))

if __name__ == '__main__':
    main()
