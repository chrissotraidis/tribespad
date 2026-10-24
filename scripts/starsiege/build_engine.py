#!/usr/bin/env python3
"""Build the headless Starsiege: Tribes 1.41 engine for macOS (goal loop G2).

Takes the lift from scripts/starsiege/lift_preview.py (run it with --no-compile
first), puts it in TribesPad's original-address model (scripts/prepare-va-model.py,
as for Tribes 2's DLLs), compiles it with TribesPad's native Windows services and
links port/starsiege/starsiege_engine.c. Imports without a native service become
explicit stops that name the import. With --run, starts the engine headless in
an isolated copy of the game and records the first stop. It never opens a window.

Usage: .venv/bin/python scripts/starsiege/build_engine.py [--run] [--timeout 60]
"""
import argparse, hashlib, importlib.util, json, os, pathlib, re, shutil, subprocess, sys, time
import pefile

R = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(R / 'scripts'))
LIFT = R / 'generated/starsiege/lift'
OUT = R / 'generated/starsiege/engine'
IMAGE_BASE, IMPORT_BASE = 0x400000, 0xfffe0000
# Host routes the guest can call (cdecl): SDL's allocator callbacks.
HOST_BASE = 0xfffd0000
HOST_ROUTES = [('StarsiegeGuestMalloc', 'ss_guest_malloc', 1), ('StarsiegeGuestCalloc', 'ss_guest_calloc', 2),
               ('StarsiegeGuestRealloc', 'ss_guest_realloc', 2), ('StarsiegeGuestFree', 'ss_guest_free', 1)]
# Reported as not present. Plugins and Windows-only helpers; DirectInput is
# optional in 1.40 (Win32 messages drive input when it is missing); DirectSound
# waits for a native provider at G3 (sound), so the menu runs silent until then.
# Optimization for the translated game (STARSIEGE_OPT=-O0 for faster iteration).
OPT = os.environ.get('STARSIEGE_OPT', '-O1')
ABSENT = ['ScriptGL.dll', 'rmdgfx.dll', 'mscoree.dll', 'dbghelp.dll', 'dInput.dll', 'dSound.dll']

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

va = load('va', R / 'scripts/prepare-va-model.py')
census = load('census', R / 'scripts/starsiege/census.py')
bindgen = load('bindgen', R / 'scripts/gen-win32-bindings.py')
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
SDK = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
TARGET = ['-target', 'arm64-apple-macos12', '-isysroot', SDK]

RUNTIME = [R / 'port/core' / (n + '.c') for n in [
    'guest_adapters', 'guest_avi', 'guest_bcrypt', 'guest_console', 'guest_console_callback', 'guest_locale_identifiers',
    'guest_locale_info', 'guest_datetime', 'guest_locales', 'guest_locale_callback', 'guest_collation', 'guest_once',
    'guest_once_callback', 'guest_address_space', 'guest_sdl_memory', 'guest_sdl_allocator', 'guest_teb', 'guest_thread_ids',
    'guest_threads', 'guest_srw', 'guest_condition', 'guest_virtual_memory', 'guest_system', 'guest_system_query',
    'guest_scheduling', 'guest_slist', 'guest_encoding', 'native_fls', 'guest_fls',
    'guest_file', 'guest_find', 'guest_display', 'guest_cursors', 'guest_icons', 'keyboard_state', 'keyboard_translation',
    'input_buffer', 'guest_dinput', 'native_exports', 'native_system_providers', 'guest_version', 'guest_cpu',
    'guest_processor_features', 'guest_heap', 'exception_filter', 'guest_raise_exception',
    'guest_strings', 'guest_messages', 'guest_user_messages', 'guest_ntstatus', 'native_handles', 'guest_profile',
    'process_environment', 'process_startup', 'guest_modules', 'guest_loader', 'gdi_memory', 'window_classes',
    'guest_callback', 'hidden_windows', 'guest_window_catalog', 'guest_tls', 'performance_clock',
    'multimedia_timer', 'guest_filesystem', 'guest_dispatch', 'quadword_conversion', 'sse_arithmetic', 'shift',
    'ext80', 'mmx', 'x87_status', 'guest_memory_checks']] + [R / 'port/apple/display.m', R / 'port/apple/font.m',
    R / 'port/starsiege/starsiege_x87.c', R / 'port/starsiege/starsiege_win32.c', R / 'port/starsiege/starsiege_heap.c',
    R / 'port/starsiege/starsiege_display.c', R / 'port/starsiege/starsiege_windows.c',
    R / 'port/starsiege/starsiege_winsock.c', R / 'port/starsiege/starsiege_registry.c',
    R / 'port/starsiege/starsiege_critical_section.c', R / 'port/starsiege/starsiege_fast_heap.c'] + [
    R / 'port/llasm-support' / (n + '.c') for n in [
    'tribes_result', 'tribes_sse', 'tribes_quadword_conversion', 'tribes_shift', 'tribes_absolute_memory', 'tribes_atomic',
    'tribes_float80', 'tribes_mmx', 'tribes_scan', 'tribes_x87']] + [
    R / 'ref/halopad/port/llasm-support' / ('llasm_' + n + '.c') for n in ['divide', 'pushx', 'movs', 'cmps', 'scas', 'stos']]

def say(message):
    print('starsiege engine: ' + message, flush=True)

def prepare(report, backend_bindings):
    lift = json.loads((LIFT / 'result.json').read_text())
    assert lift['stages']['srw']['exit'] == 0, 'run scripts/starsiege/lift_preview.py --no-compile first'
    image = LIFT / 'Tribes141.exe'
    assert sha(image) == lift['image_sha256']
    pe = pefile.PE(str(image))
    assert pe.OPTIONAL_HEADER.ImageBase == IMAGE_BASE and not hasattr(pe, 'DIRECTORY_ENTRY_TLS')
    main = (LIFT / 'tribes141.llasm').read_text().splitlines()
    extern = (LIFT / 'extern.llinc').read_text()
    known = set(re.findall(r'^proc (\S+) external', '\n'.join(main) + '\n' + extern, re.M))
    imports, names = [], set()
    for dll in pe.DIRECTORY_ENTRY_IMPORT:
        library = dll.dll.decode()
        ordinals = census.WS2_ORDINALS if library.upper() == 'WS2_32.DLL' else {17: 'InitCommonControls'} if library.upper() == 'COMCTL32.DLL' else {}
        for imp in dll.imports:
            if imp.import_by_ordinal:
                # pefile names some ordinals itself; SRW spells them LIB_ordN or by
                # its own table. Use whichever spelling the translated code declares.
                ordinal = library.split('.')[0] + '_ord' + str(imp.ordinal)
                friendly = imp.name.decode() if imp.name else ordinals.get(imp.ordinal)
                sr_name = next((n for n in [ordinal, friendly] if n and n in known), ordinal)
                name = friendly or ordinal
            else:
                sr_name = name = imp.name.decode()
            assert name not in names, 'duplicate import ' + name
            names.add(name)
            imports.append(dict(slot=imp.address, name=name, sr_name=sr_name, dll=library.lower(), symbol=va.symbol(name)))
    imports.sort(key=lambda r: r['slot'])
    values = {}
    for i, row in enumerate(imports):
        row['guest_va'] = IMPORT_BASE + 16 * i
        values[row['name']] = values[row['sr_name']] = row['guest_va']
    aliases = sorted({'define %s %s' % (n, row['symbol']) for row in imports for n in {row['name'], row['sr_name']}})
    extern = '\n'.join(l for l in extern.splitlines() if not (re.fullmatch(r'proc (\S+) external', l) and l.split()[1] in values))
    (OUT / 'extern.llinc').write_text('\n'.join(aliases + ['proc %s external' % r['symbol'] for r in imports]) + '\n' + extern +
                                      '\nproc tribes_dispatch external\nfuncv tribes_set_dispatch_target $, target\n')
    (OUT / 'macros.llinc').write_text('')
    code = '\n'.join(p.read_text() for p in sorted(LIFT.glob('seg*_code.llinc')))
    assert not re.search(r'^proc \?', code, re.M)
    defined = set(re.findall(r'^proc (loc_[0-9A-F]+)', code, re.M))
    targets = set(re.findall(r'^\s*(?:tcall|ctcallz|ctcallnz)\s+.*?\b(loc_[0-9A-F]+)\s*$', code, re.M))
    missing = sorted(targets - defined)
    for name in missing:
        code += '\nproc %s\ncall halopad_trap_unimplemented %s\nendp\n' % (name, hex(int(name[4:], 16)))
    report['direct_targets_trapped'] = missing
    code, stats = va.transform(code.splitlines(), values)
    (OUT / 'code.llinc').write_text('\n'.join(code) + '\n')
    kept, skip, included = [], False, False
    for line in main:
        if re.fullmatch(r'proc (\S+) external', line) and line.split()[1] in values:
            continue
        if line.startswith('datasegment '):
            skip = True
            continue
        if skip:
            skip = not line.startswith('endd')
            continue
        if line.startswith('define '):
            continue
        if re.fullmatch(r'include seg\d+_code\.llinc', line):
            if not included:
                kept.append('include code.llinc')
                included = True
            continue
        if re.fullmatch(r'include seg\d+_data\.llinc', line):
            continue
        kept.append(line)
    assert included
    (OUT / 'module.llasm').write_text('\n'.join(kept) + '\n')
    # llasm is the slow step; skip it when its inputs are unchanged.
    key = hashlib.sha256(b''.join(pathlib.Path(p).read_bytes() for p in
                                  [OUT / 'module.llasm', OUT / 'code.llinc', OUT / 'extern.llinc'])).hexdigest()
    cached = OUT / 'module.json'
    if cached.exists() and (OUT / 'module.ll').exists() and json.loads(cached.read_text()).get('key') == key:
        procs, removed = json.loads(cached.read_text())['procs'], 0
    else:
        with (OUT / 'llasm.log').open('w') as log:
            subprocess.run([str(R / 'generated/tools/llasm/llasm'), '-m64', '-ptrofs', '-O2', '-I', str(OUT), '-I', str(R / 'port/llasm-support'),
                            '-I', str(R / 'ref/halopad/port/llasm-support'), '-o', str(OUT / 'raw.ll'), str(OUT / 'module.llasm')],
                           cwd=OUT, stdout=log, stderr=log, check=True)
        procs, removed = va.postprocess(OUT / 'raw.ll', OUT / 'module.ll')
        (OUT / 'raw.ll').unlink()
        cached.write_text(json.dumps({'key': key, 'procs': procs}))
    routes = [(HOST_BASE + 16 * i, name, helper, n) for i, (name, helper, n) in enumerate(HOST_ROUTES)]
    # OpenGL imports pass through a counting thunk (logged with STARSIEGE_TRACE_GL=1).
    entries = sorted([(int(n[4:], 16), n) for n in procs] + [(r['guest_va'], r['symbol']) for r in imports] +
                     [(address, 'tbimp_' + name) for address, name, _, _ in routes])
    assert len(entries) == len({v for v, _ in entries})
    entry = IMAGE_BASE + pe.OPTIONAL_HEADER.AddressOfEntryPoint
    assert entry in {v for v, _ in entries}
    (OUT / 'dispatch.ll').write_text(va.dispatch_ir(entries))
    mapped = pe.get_memory_mapped_image()
    (OUT / 'image.bin').write_bytes(mapped + bytes(pe.OPTIONAL_HEADER.SizeOfImage - len(mapped)))
    bindings = json.loads((R / 'port/llasm-runtime/win32-bindings.json').read_text())
    extra = json.loads((R / 'config/starsiege/bindings.json').read_text())
    starsiege, overrides = extra['bindings'], extra.get('overrides', {})
    assert not set(starsiege) & set(bindings), 'new Starsiege services go in "bindings"; replacements in "overrides"'
    assert set(overrides) <= set(bindings) and all(o.get('why') for o in overrides.values())
    bindings.update(starsiege)
    bindings.update({n: {k: v for k, v in o.items() if k != 'why'} for n, o in overrides.items()})
    # SDL, GL, DirectSound and OpenAL wrappers come from TribesPad's SDL backend.
    # Starsiege may replace only its logged no-op entries (gl-unprovided.json):
    # Tribes 2 never creates WGL contexts, Starsiege does.
    replaced = set(bindings) & set(backend_bindings)
    assert all(backend_bindings[n]['helper'].startswith('tribes_gl_unprovided_') for n in replaced), sorted(replaced)
    wrappers = OUT / 'backend/graphics-bindings.ll'
    text = wrappers.read_text()
    for name in sorted(replaced):
        text, count = re.subn(r'^define hidden fastcc void @tbimp_%s\(ptr %%cpu\) \{\n.*?^\}\n' % re.escape(name), '', text, flags=re.M | re.S)
        assert count == 1, name
    wrappers.write_text(text)
    report['backend_entries_replaced'] = sorted(replaced)
    bound = {r['name']: 1 for r in imports if r['name'] in bindings or r['name'] in backend_bindings}
    # Every wrapper: the native export table (GetProcAddress) refers to all of them.
    host = {name: {'helper': helper, 'arguments': n, 'returns': True, 'guest_base': True, 'calling_convention': 'cdecl'}
            for _, name, helper, n in routes}
    (OUT / 'bindings.ll').write_text(bindgen.generate({**bindings, **host}))
    stops = ['declare void @starsiege_missing_import(ptr, i32) noreturn']
    for i, row in enumerate(imports):
        if row['name'] not in bound:
            stops.append('define hidden fastcc void @%s(ptr %%cpu) {\n call void @starsiege_missing_import(ptr %%cpu, i32 %d)\n unreachable\n}' % (row['symbol'], i))
    (OUT / 'stops.ll').write_text('\n'.join(stops) + '\n')
    header = lambda name, data: 'static const unsigned char %s[]={%s};\n' % (name, ','.join(map(str, data)))
    env = json.loads((R / 'config/runtime/environment.json').read_text())
    variables = b''.join((k + '=' + v).encode(env['encoding']) + b'\0' for k, v in sorted(env['variables'].items(), key=lambda kv: kv[0].casefold())) + b'\0'
    command = '"C:\\TribesPad\\Game\\Tribes.exe"'
    inc = ['#define SS_IMAGE_BASE 0x%xu' % IMAGE_BASE, '#define SS_IMAGE_SIZE %du' % pe.OPTIONAL_HEADER.SizeOfImage,
           '#define SS_STACK_RESERVE %du' % max(pe.OPTIONAL_HEADER.SizeOfStackReserve, 0x10000), '#define SS_ENTRY 0x%xu' % entry,
           '#define SS_WINDOWS_DIRECTORY ' + json.dumps(env['legacy_profile']['windows_directory']),
           '#define SS_PROFILE_FILE ' + json.dumps(env['legacy_profile']['file']),
           header('environment', variables), header('command_line', command.encode('cp1252') + b'\0'),
           header('wide_command_line', command.encode('utf-16le') + b'\0\0'),
           'static const uint32_t imports[][2]={%s};' % ','.join('{%du,%du}' % (r['slot'], r['guest_va']) for r in imports),
           'static const char *const import_names[]={%s};' % ','.join(json.dumps(r['dll'] + '!' + r['name']) for r in imports),
           'static const char *const absent_modules[]={%s};' % ','.join(json.dumps(n) for n in ABSENT)]
    inc.append('static const uint32_t sdl_allocator[4]={%s};' % ','.join('%du' % address for address, _, _, _ in routes))
    # Count GL calls (STARSIEGE_TRACE_GL=1) inside every GL wrapper, so direct and
    # pointer calls are both seen.
    import graphics_backend_inputs
    names = [row[0] for row in graphics_backend_inputs.EXPORTS]
    index = {n: i for i, n in enumerate(names)}
    for wrapper in [OUT / 'bindings.ll', OUT / 'backend/graphics-bindings.ll']:
        text = wrapper.read_text()
        text = re.sub(r'^(define hidden fastcc void @tbimp_([A-Za-z0-9_]+)\(ptr %cpu\) \{\n)',
                      lambda m: m[1] + (' call void @ss_gl_trace(i32 %d)\n' % index[m[2]] if m[2] in index else ''), text, flags=re.M)
        if 'declare void @ss_gl_trace(i32)' not in text:
            text = 'declare void @ss_gl_trace(i32)\n' + text
        wrapper.write_text(text)
    inc.append('static const char *const gl_trace_names[]={%s};' % ','.join(json.dumps(n) for n in names))
    (OUT / 'starsiege_engine.inc').write_text('\n'.join(inc) + '\n')
    report.update(imports=len(imports), bound=len(bound), stops=len(imports) - len(bound), procedures=len(procs),
                  entry=entry, va_stats=stats, wrappers_removed=removed)
    (OUT / 'imports.json').write_text(json.dumps(imports, indent=1) + '\n')

def backend():
    """TribesPad's SDL backend: GL (through GL4ES and ANGLE), DirectSound and OpenAL."""
    from sdl_backend_inputs import prepare as sdl_prepare
    from gl4es_backend_inputs import inputs as gl4es_inputs
    from angle_backend_inputs import inputs as angle_inputs
    folder = OUT / 'backend'
    folder.mkdir(parents=True, exist_ok=True)
    sources, flags, libraries, bindings, _ = sdl_prepare(folder)
    libraries = [x for i, x in enumerate(libraries) if not (x == 'OpenGL' or (x == '-framework' and i + 1 < len(libraries) and libraries[i + 1] == 'OpenGL'))]
    _, gl_flags, gl_libraries = gl4es_inputs('macos')
    _, angle_flags, angle_libraries = angle_inputs('macos')
    angle_dir = str(pathlib.Path(angle_libraries[0]).parent)
    angle_libraries = [x for x in angle_libraries if not x.startswith('-Wl,-rpath')] + ['-Wl,-rpath,' + angle_dir]
    host = [R / 'port/apple/gl4es_context.c', R / 'port/apple/angle_sdl.c', R / 'port/starsiege/starsiege_gl.c']
    flags = flags + gl_flags + angle_flags + ['-I', str(folder), '-isystem', str(R / 'ref/gl4es/src'),
                                              '-DNOX11', '-DNOEGL', '-DNO_LOADER', '-DDEFAULT_ES=2']
    return {'sources': [pathlib.Path(s) for s in sources] + host, 'flags': flags,
            'libraries': angle_libraries + gl_libraries + libraries, 'bindings': bindings}

def compile_objects(report, graphics):
    objects, cache_path = [], OUT / 'objects.json'
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    math = load('sse', R / 'scripts/sse_build_inputs.py').sse_inputs('macos')
    flags = ['-O2', '-DPTROFS_64BIT', '-Wno-override-module', '-Wall', '-Wextra', '-Werror'] + math['compile_flags']
    for source in RUNTIME + graphics['sources'] + [OUT / 'dispatch.ll', OUT / 'bindings.ll', OUT / 'stops.ll', R / 'port/starsiege/starsiege_engine.c']:
        obj = OUT / 'obj' / (source.stem + '.o')
        obj.parent.mkdir(exist_ok=True)
        key = sha(source) + (sha(OUT / 'starsiege_engine.inc') if source.name == 'starsiege_engine.c' else '') + ' '.join(graphics['flags'])
        if cache.get(str(source)) != key or not obj.exists():
            cmd = ['clang'] + TARGET + flags + graphics['flags'] + ['-I', str(OUT), '-c', str(source), '-o', str(obj)]
            if source.suffix == '.ll':
                cmd = ['clang'] + TARGET + ['-O2', '-Wno-override-module', '-c', str(source), '-o', str(obj)]
            if source.suffix == '.m':
                cmd.insert(1, '-fobjc-arc')
            subprocess.run(cmd, check=True)
            cache[str(source)] = key
        objects.append(str(obj))
    module = OUT / 'obj/module.o'
    if cache.get('module') != sha(OUT / 'module.ll') + OPT or not module.exists():
        say('compiling the translated game (a few minutes)')
        start = time.monotonic()
        subprocess.run(['clang'] + TARGET + [OPT, '-Wno-override-module', '-c', str(OUT / 'module.ll'), '-o', str(module)], check=True)
        report['module_compile_seconds'] = round(time.monotonic() - start)
        cache['module'] = sha(OUT / 'module.ll') + OPT
    cache_path.write_text(json.dumps(cache, indent=1) + '\n')
    return objects + [str(module)] + math['libraries'] + graphics['libraries']

def link(objects, report):
    exe = OUT / 'starsiege-engine'
    with (OUT / 'link.log').open('w') as log:
        result = subprocess.run(['clang'] + TARGET + objects + [str(R / 'generated/bochs-math/math_bridge.o'),
                                str(R / 'generated/bochs-math/libbochs-math.a'), '-lc++',
                                '-lz'] + [x for f in ['Foundation', 'CoreGraphics', 'CoreText', 'CoreFoundation', 'AppKit', 'Metal',
                                'QuartzCore', 'GameController', 'AVFoundation', 'AudioToolbox', 'CoreAudio', 'SystemConfiguration',
                                'ImageIO', 'IOKit', 'CoreHaptics', 'UniformTypeIdentifiers'] for x in ['-framework', f]] + ['-o', str(exe)],
                                stdout=log, stderr=log)
    report['link_exit'] = result.returncode
    if result.returncode:
        undefined = sorted(set(re.findall(r'"_([^"]+)", referenced from', (OUT / 'link.log').read_text())))
        report['undefined_symbols'] = undefined
        return None
    report['engine_sha256'] = sha(exe)
    return exe

def run(exe, timeout, report, route=None):
    work = OUT / 'run'
    if work.exists():
        shutil.rmtree(work)
    game, temp = work / 'volume/TribesPad/Game', work / 'volume/TribesPad/Temp'
    shutil.copytree(R / 'ref/starsiege/game', game)
    # The installer carries its packager's stale server state; a fresh game has none.
    for stale in (game / 'temp').glob('serverTempData*.cs'):
        stale.unlink()
    # Diagnostics in the run copy only: the game's own console log (console.log).
    script = game / 'console.cs'
    script.write_text('$Console::LogMode = 1;\n' + script.read_text(errors='replace'))
    if route:
        # A scripted route for headless checks, appended to the run copy only.
        script.write_text(script.read_text(errors='replace') + '\n' + route + '\n')
    temp.mkdir(parents=True)
    say('running headless (no window)')
    capture = work / 'frames'
    capture.mkdir()
    environment = dict(os.environ, STARSIEGE_HEADLESS='1', STARSIEGE_CAPTURE=str(capture), STARSIEGE_TRACE_GL='1')
    try:
        result = subprocess.run([str(exe), str(OUT / 'image.bin'), str(work / 'volume'), str(game), str(temp)],
                                capture_output=True, text=True, timeout=timeout, env=environment)
        stdout, stderr, code = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr, code = (error.stdout or b'').decode(errors='replace'), (error.stderr or b'').decode(errors='replace'), 'timeout'
    (work / 'stdout.log').write_text(stdout)
    (work / 'stderr.log').write_text(stderr)
    lines = [l for l in stdout.splitlines() if l.startswith('{')]
    report['run'] = {'exit': code, 'result': json.loads(lines[-1]) if lines else None, 'stderr_tail': stderr[-1500:]}

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--run', action='store_true')
    ap.add_argument('--timeout', type=int, default=60)
    ap.add_argument('--route', help='TorqueScript appended to the run copy of console.cs (headless checks)')
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    report = {'playable': False, 'headless': True}
    graphics = backend()
    prepare(report, graphics['bindings'])
    exe = link(compile_objects(report, graphics), report)
    if exe and a.run:
        run(exe, a.timeout, report, a.route)
    (OUT / 'result.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'direct_targets_trapped'}, indent=1))
    return 0 if exe else 1

if __name__ == '__main__':
    sys.exit(main())

