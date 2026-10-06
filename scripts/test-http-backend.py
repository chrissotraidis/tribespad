#!/usr/bin/env python3
"""Exercise the pinned macOS backend against local real HTTP and TLS servers."""
import gzip
import argparse
import hashlib
import importlib.util
import http.server
import json
import pathlib
import ssl
import subprocess
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated/integration-review/http-backend'
BUILD = ROOT / 'generated/http-backend/macos'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
BODY = bytes((i * 37 + 11) % 256 for i in range(65539))


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    def handle_error(self, request, client_address):
        # Clients intentionally cancel timeout/write-callback tests.
        import sys
        if not isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
            super().handle_error(request, client_address)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == '/slow':
            time.sleep(0.2)
        data = BODY
        self.send_response(302 if self.path == '/redirect' else 200)
        if self.path == '/redirect':
            self.send_header('Location', '/gzip')
            data = b''
        elif self.path == '/gzip':
            data = gzip.compress(data)
            self.send_header('Content-Encoding', 'gzip')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        assert self.path == '/echo'
        assert self.headers['Content-Type'] == 'application/octet-stream'
        assert self.headers['X-TribesPad-Test'] == 'binary'
        size = int(self.headers['Content-Length'])
        assert size == 4099
        data = self.rfile.read(size)
        assert data == BODY[:size]
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    global OUT
    parser = argparse.ArgumentParser()
    parser.add_argument('--guest-allocator', action='store_true')
    parser.add_argument('--checked', action='store_true')
    args = parser.parse_args()
    if args.guest_allocator:
        OUT = ROOT / 'generated/integration-review/curl-allocator'
        if args.checked:
            OUT /= 'checked-run'
    assert not args.checked or args.guest_allocator
    OUT.mkdir(parents=True, exist_ok=True)
    result = OUT / 'result.json'
    if result.exists():
        result.unlink()
    build_report = json.loads((BUILD / 'result.json').read_text())
    assert build_report['passed'] and build_report['platform'] == 'macos'
    for name, expected in build_report['libraries'].items():
        assert sha(ROOT / name) == expected, name
    assert sha(ROOT / 'config/runtime/http-backend.json') == build_report['config_sha256']
    # Ephemeral local test CA/server certificate. Not an application trust root.
    certificate = OUT / 'localhost.crt'
    key = OUT / 'localhost.key'
    config = OUT / 'certificate.cnf'
    config.write_text('[req]\ndistinguished_name=dn\nx509_extensions=extensions\nprompt=no\n'
                      '[dn]\nCN=localhost\n[extensions]\nsubjectAltName=DNS:localhost\n'
                      'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature,keyEncipherment,keyCertSign\n'
                      'extendedKeyUsage=serverAuth\n')
    with (OUT / 'certificate.log').open('w') as log:
        subprocess.run(['/usr/bin/openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-days', '1', '-config', str(config), '-keyout', str(key), '-out', str(certificate)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    key.chmod(0o600)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(str(certificate), str(key))
    plain = Server(('127.0.0.1', 0), Handler)
    tls = Server(('127.0.0.1', 0), Handler)
    tls.socket = context.wrap_socket(tls.socket, server_side=True)
    servers = [plain, tls]
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in servers]
    for thread in threads:
        thread.start()
    source = ROOT / 'tests/http_backend_test.c'
    prefix = BUILD / 'install'
    executable = OUT / 'http-backend-test'
    command = ['clang', '-target', 'arm64-apple-macos12', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-I', str(prefix / 'include'), str(source),
               *[str(prefix / 'lib' / name) for name in ['libcurl.a', 'libssl.a', 'libcrypto.a']],
               '-lz', '-framework', 'SystemConfiguration', '-framework', 'CoreFoundation',
               '-framework', 'CoreServices', '-o', str(executable)]
    sources = [source]
    if args.guest_allocator:
        spec = importlib.util.spec_from_file_location('bindings', ROOT / 'scripts/gen-win32-bindings.py')
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        bindings = {name: dict(helper=helper, arguments=count, guest_base=False, cpu_argument=True,
                               returns=returns, helper_returns='i32' if returns else 'void', calling_convention='cdecl')
                    for name, helper, count, returns in [
                        ('CurlFixtureMalloc', 'curl_fixture_malloc', 1, True),
                        ('CurlFixtureFree', 'curl_fixture_free', 1, False),
                        ('CurlFixtureRealloc', 'curl_fixture_realloc', 2, True),
                        ('CurlFixtureStrdup', 'curl_fixture_strdup', 1, True),
                        ('CurlFixtureCalloc', 'curl_fixture_calloc', 2, True)]}
        wrapper = OUT / 'bindings.ll'
        wrapper.write_text(generator.generate(bindings, False))
        sources += [ROOT / name for name in ['port/core/guest_curl_allocator.c', 'port/core/guest_callback.c',
                                            'port/core/guest_heap.c', 'port/core/guest_memory_checks.c',
                                            'tests/fixtures/curl_allocator_driver.c', 'tests/fixtures/native_enter.ll']]
        sources += [wrapper]
        command += ['-DTRIBES_CURL_GUEST_ALLOCATOR_TEST', '-DPTROFS_64BIT', '-Wno-override-module', *map(str, sources[1:])]
        if args.checked:
            from checked_support import compile_support
            from guard_checked_link import guard_link
            trusted = [ROOT / 'port/core/guest_memory_checks.c', ROOT / 'port/core/guest_external_checks.c']
            selected = [p for p in sources[1:] if p not in trusted]
            objects, _ = compile_support(selected, OUT / 'checked', ['-DCHECKED_TEST', '-DPTROFS_64BIT'])
            meta = json.loads((OUT / 'checked/result.json').read_text())
            entries = []
            for name in objects:
                p = pathlib.Path(name)
                entries.append(dict(object=name, object_sha256=sha(p), bitcode=str(p.with_suffix('.bc')),
                                    bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
            objects, _ = guard_link(entries, OUT / 'guarded')
            command = [c for c in command if c not in list(map(str, sources[1:]))]
            command += ['-DCHECKED_TEST', *objects, *map(str, trusted)]
            sources += [ROOT / 'port/core/guest_external_checks.c']
    try:
        subprocess.run(command, check=True)
        linked = subprocess.check_output(['otool', '-L', str(executable)], text=True)
        (OUT / 'linked-libraries.txt').write_text(linked)
        assert 'libcurl' not in linked and 'libssl' not in linked and 'libcrypto' not in linked and '/opt/' not in linked
        run_command = [str(executable), 'http://127.0.0.1:' + str(plain.server_port),
                       'https://localhost:' + str(tls.server_port),
                       'https://127.0.0.1:' + str(tls.server_port), str(certificate)]
        run = subprocess.run(run_command, capture_output=True, text=True, timeout=30)
        (OUT / 'stdout.log').write_text(run.stdout)
        (OUT / 'stderr.log').write_text(run.stderr)
        assert run.returncode == 0 and 'PASS:' in run.stdout, (run.returncode, run.stdout, run.stderr)
        faults = {}
        if args.guest_allocator:
            for name in ['outside-frame', 'unknown-pointer', 'destroy-live', 'wrong-thread', 'missing-suspend',
                         'protected-export', 'protected-import', 'short-stack', 'allocator-reentry', 'guest-native-string',
                         'opaque-corruption', 'bad-calloc', 'bad-realloc', 'bad-strdup']:
                fault = subprocess.run(run_command + [name], capture_output=True, text=True, timeout=15)
                (OUT / (name + '.log')).write_text(fault.stdout + fault.stderr)
                assert fault.returncode == -6, (name, fault.returncode, fault.stderr)
                expected = 'checked guest memory violation' if name.startswith('protected-') else 'invalid curl allocator ownership/frame'
                assert expected in fault.stderr, (name, fault.stderr)
                faults[name] = dict(returncode=fault.returncode, diagnostic=expected)
        report = dict(passed=True, build_report_sha256=sha(BUILD / 'result.json'), command=command,
                      run_command=run_command, binary_sha256=sha(executable),
                      source_sha256=sha(source), script_sha256=sha(pathlib.Path(__file__)),
                      sources_sha256={str(p.relative_to(ROOT)): sha(p) for p in sources},
                      headers_sha256=({n: sha(ROOT / n) for n in ['port/core/guest_curl_allocator.h',
                                         'port/core/guest_callback.h', 'port/llasm-support/llasm_cpu.h',
                                         'tests/fixtures/curl_allocator_driver.h']} if args.guest_allocator else {}),
                      checked=args.checked, faults=faults,
                      certificate_sha256=sha(certificate), stdout=run.stdout,
                      scope=('Real localhost HTTP/HTTPS through mirrored allocations and emitted cdecl guest allocator fixtures. Manual transfer guards enabled. Original QoL allocators, whole-program external memory coverage, guest curl API/socket callbacks, simulator and game authentication remain unverified.' if args.guest_allocator else
                             'Real localhost HTTP/HTTPS with native owner-thread allocator callbacks, socket/timer callbacks, gzip redirect, binary POST, CA/hostname rejection, timeout, callback abort and allocation failures. No guest allocator, guest memory guard, simulator execution or game authentication claim.'))
        result.write_text(json.dumps(report, indent=2) + '\n')
        print(run.stdout, end='')
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=2)
        key.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
