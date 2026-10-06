#!/usr/bin/env python3
"""Fetch verified sources and build a static HTTP/TLS backend, one Apple target.

This is a native dependency build, not a guest libcurl implementation. Sources
stay pristine: every source file is checked against the pinned archive on each
run. Build products and commands are retained outside the source tree.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = ROOT / 'config/runtime/http-backend.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source(pin):
    archive, tree = ROOT / pin['archive'], ROOT / pin['path']
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix(archive.suffix + '.download')
        with urllib.request.urlopen(pin['url'], timeout=60) as response:
            temporary.write_bytes(response.read())
        if sha(temporary) != pin['sha256']:
            raise RuntimeError('download checksum mismatch: ' + str(temporary))
        temporary.replace(archive)
    if sha(archive) != pin['sha256']:
        raise RuntimeError('archive checksum mismatch: ' + str(archive))
    files = {}
    with tarfile.open(archive) as tar:
        # These distributions have no links. Refuse traversal and special files.
        members = tar.getmembers()
        for member in members:
            path = pathlib.PurePosixPath(member.name)
            if (path.is_absolute() or '..' in path.parts or not path.parts or
                    path.parts[0] != tree.name or
                    not (member.isdir() or member.isfile())):
                raise RuntimeError('unexpected archive entry: ' + member.name)
        if not tree.exists():
            tar.extractall(tree.parent, members=members)
        for member in members:
            if not member.isfile():
                continue
            relative = str(pathlib.PurePosixPath(member.name).relative_to(tree.name))
            expected = hashlib.sha256(tar.extractfile(member).read()).hexdigest()
            path = tree / relative
            if path.is_symlink() or not path.is_file() or sha(path) != expected:
                raise RuntimeError('modified source: ' + str(path))
            files[relative] = expected
    actual = {str(p.relative_to(tree)) for p in tree.rglob('*') if p.is_file() or p.is_symlink()}
    if actual != set(files):
        raise RuntimeError('untracked source files: ' + str(actual - set(files)))
    return tree, files


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--platform', choices=['macos', 'device', 'simulator'], default='macos')
    args = parser.parse_args()
    config = json.loads(CONFIG.read_text())
    sources = [source(pin) for pin in config['sources']]
    curl, tls = [item[0] for item in sources]
    out = ROOT / 'generated/http-backend' / args.platform
    out.mkdir(parents=True, exist_ok=True)
    # Invalidate previous success before starting a new build.
    result = out / 'result.json'
    if result.exists():
        result.unlink()
    sdk_name = dict(macos='macosx', device='iphoneos', simulator='iphonesimulator')[args.platform]
    sdk = subprocess.check_output(['xcrun', '--sdk', sdk_name, '--show-sdk-path'], text=True).strip()
    compiler = subprocess.check_output(['xcrun', '--sdk', sdk_name, '--find', 'clang'], text=True).strip()
    common = ['-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF',
              '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DPKG_CONFIG_EXECUTABLE=/usr/bin/false',
              '-DCMAKE_C_COMPILER=' + compiler, '-DCMAKE_OSX_ARCHITECTURES=arm64',
              '-DCMAKE_OSX_SYSROOT=' + sdk,
              '-DCMAKE_OSX_DEPLOYMENT_TARGET=' + ('12.0' if args.platform == 'macos' else '16.0')]
    if args.platform != 'macos':
        common += ['-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY']
    commands = []

    def run(name, command):
        commands.append(command)
        with (out / (name + '.log')).open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)

    prefix = out / 'install'
    run('tls-configure', ['cmake', '--fresh', '-S', str(tls), '-B', str(out / 'tls-build'), *common,
                         '-DLIBRESSL_APPS=OFF', '-DLIBRESSL_TESTS=OFF',
                         '-DCMAKE_INSTALL_PREFIX=' + str(prefix)])
    run('tls-build', ['cmake', '--build', str(out / 'tls-build'), '--parallel', '4'])
    run('tls-install', ['cmake', '--install', str(out / 'tls-build')])
    run('curl-configure', ['cmake', '--fresh', '-S', str(curl), '-B', str(out / 'curl-build'), *common,
                          '-DBUILD_STATIC_LIBS=ON', '-DBUILD_CURL_EXE=OFF', '-DBUILD_TESTING=OFF',
                          '-DBUILD_LIBCURL_DOCS=OFF', '-DBUILD_MISC_DOCS=OFF',
                          '-DENABLE_CURL_MANUAL=OFF', '-DCURL_USE_PKGCONFIG=OFF',
                          '-DCURL_USE_CMAKECONFIG=OFF', '-DCURL_USE_OPENSSL=ON',
                          '-DOPENSSL_USE_STATIC_LIBS=TRUE', '-DOPENSSL_ROOT_DIR=' + str(prefix),
                          '-DOPENSSL_INCLUDE_DIR=' + str(prefix / 'include'),
                          '-DOPENSSL_SSL_LIBRARY=' + str(prefix / 'lib/libssl.a'),
                          '-DOPENSSL_CRYPTO_LIBRARY=' + str(prefix / 'lib/libcrypto.a'),
                          '-DUSE_APPLE_SECTRUST=OFF', '-DCURL_CA_BUNDLE=none', '-DCURL_CA_PATH=none',
                          '-DENABLE_THREADED_RESOLVER=OFF', '-DENABLE_ARES=OFF',
                          '-DHTTP_ONLY=ON', '-DCURL_ZLIB=ON',
                          '-DZLIB_INCLUDE_DIR=' + sdk + '/usr/include',
                          '-DZLIB_LIBRARY=' + sdk + '/usr/lib/libz.tbd',
                          '-DCURL_BROTLI=OFF', '-DCURL_ZSTD=OFF', '-DUSE_NGHTTP2=OFF',
                          '-DCURL_USE_LIBSSH2=OFF', '-DCURL_USE_LIBSSH=OFF',
                          '-DUSE_LIBIDN2=OFF', '-DCURL_USE_LIBPSL=OFF',
                          '-DCMAKE_INSTALL_PREFIX=' + str(prefix)])
    run('curl-build', ['cmake', '--build', str(out / 'curl-build'), '--parallel', '4'])
    run('curl-install', ['cmake', '--install', str(out / 'curl-build')])
    libraries = [prefix / 'lib' / name for name in ['libcurl.a', 'libssl.a', 'libcrypto.a']]
    pkgconfig = (prefix / 'lib/pkgconfig/libcurl.pc').read_text()
    assert 'supported_protocols="HTTP HTTPS"' in pkgconfig, pkgconfig
    for build in ['tls-build', 'curl-build']:
        compile_commands = (out / build / 'compile_commands.json').read_text()
        if '/opt/homebrew/' in compile_commands or '/usr/local/' in compile_commands:
            raise RuntimeError('unexpected host dependency in ' + build)
    architectures = {}
    for library in libraries:
        architectures[library.name] = subprocess.check_output(['lipo', '-archs', str(library)], text=True).strip()
        if architectures[library.name] != 'arm64':
            raise RuntimeError('wrong archive architecture: ' + str(library))
    report = dict(passed=True, platform=args.platform, sdk=sdk, commands=commands,
                  source_archives={pin['archive']: pin['sha256'] for pin in config['sources']},
                  source_files={pin['path']: files for pin, (_, files) in zip(config['sources'], sources)},
                  libraries={str(p.relative_to(ROOT)): sha(p) for p in libraries},
                  architectures=architectures, config_sha256=sha(CONFIG), script_sha256=sha(pathlib.Path(__file__)),
                  pkgconfig=pkgconfig,
                  scope=config['scope'], execution_tested=False)
    result.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: static ARM64 curl/LibreSSL dependency build (' + args.platform + '); execution not tested')


if __name__ == '__main__':
    main()
