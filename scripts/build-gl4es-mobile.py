#!/usr/bin/env python3
"""Build the pinned GL4ES dependency for ARM64 Apple mobile renderer evaluation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
COMMIT = 'ec16bedd8819c475326f4f1a3063772c6d986e06'
URL = 'https://github.com/ptitSeb/gl4es.git'


def build(platform):
    env = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
    ref = ROOT / 'ref/gl4es'
    if not ref.exists():
        subprocess.run(['git', 'clone', URL, str(ref)], check=True)
        subprocess.run(['git', '-C', str(ref), 'checkout', '--detach', COMMIT], check=True)
    revision = subprocess.check_output(['git', '-C', str(ref), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != COMMIT:
        raise RuntimeError('GL4ES reference does not match pinned commit')
    subprocess.run(['git', '-C', str(ref), 'diff', '--exit-code', 'HEAD'], check=True)
    source = ROOT / 'generated/gl4es/source'
    # Keep upstream untouched. These two GNU aliases need private C forwarding
    # functions on Darwin; no rendering behavior is changed.
    shutil.copytree(ref, source, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('.git', 'lib', 'bin'))
    original = (ref / 'src/gl/directstate.c').read_text()
    patched = original
    for operation in ('Enable', 'Disable'):
        alias = ('AliasDecl(void,gl4es_gl%sClientStatei,(GLenum array, GLuint index),'
                 'gl4es_gl%sClientStateIndexed);') % (operation, operation)
        assert patched.count(alias) == 1
        wrapper = ('void APIENTRY_GL4ES gl4es_gl%sClientStatei(GLenum array, GLuint index) '
                   '{ gl4es_gl%sClientStateIndexed(array, index); }') % (operation, operation)
        patched = patched.replace(alias, wrapper)
    target = source / 'src/gl/directstate.c'
    if target.read_text() != patched:
        target.write_text(patched)
    # GLES2 cannot answer this desktop client-array query. Return GL4ES's
    # actual per-context client selector, including changes made internally.
    getter = (ref / 'src/gl/getter.c').read_text()
    anchor = 'int gl4es_commonGet(GLenum pname, GLfloat *params) {\n    switch (pname) {'
    assert getter.count(anchor) == 1 and 'case GL_CLIENT_ACTIVE_TEXTURE:' not in getter
    getter = getter.replace(anchor, anchor + '\n        case GL_CLIENT_ACTIVE_TEXTURE:\n            *params = GL_TEXTURE0 + glstate->texture.client;\n            break;')
    getter_target = source / 'src/gl/getter.c'
    if getter_target.read_text() != getter:
        getter_target.write_text(getter)
    out = ROOT / 'generated/gl4es' / platform
    sdk = 'iphonesimulator' if platform == 'simulator' else 'iphoneos'
    command = ['cmake', '-S', str(source), '-B', str(out / 'apple-build'),
               '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_SYSROOT=' + sdk,
               '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=16.0',
               '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE=' + str(out / 'lib'),
               '-DNOX11=ON', '-DNOEGL=ON', '-DSTATICLIB=ON', '-DNO_LOADER=ON',
               '-DNO_INIT_CONSTRUCTOR=ON', '-DDEFAULT_ES=2']
    subprocess.run(command, env=env, check=True)
    subprocess.run(['cmake', '--build', str(out / 'apple-build'), '-j', '4'], env=env, check=True)
    library = out / 'lib/libGL.a'
    shutil.copy2(ref / 'LICENSE', out / 'LICENSE.gl4es')
    receipt = dict(url=URL, commit=COMMIT, platform=platform, configure=command,
                   private_patch_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                   client_texture_query_patch_sha256=hashlib.sha256(getter_target.read_bytes()).hexdigest(),
                   library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                   execution_tested=False)
    (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return library


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['simulator', 'device'], default='simulator')
    print(build(parser.parse_args().platform))
