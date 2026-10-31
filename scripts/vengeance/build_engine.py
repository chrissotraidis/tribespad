#!/usr/bin/env python3
"""Build the Tribes: Vengeance engine host for macOS (goal loop M1 onward).

Takes the lifts from scripts/vengeance/lift.py and links them into one native
program in TribesPad's original-address model:

- every module keeps its rebased address (config/vengeance/layout.json);
- an import from another Vengeance module resolves to that module's export
  address, code or data (Unreal imports data such as GMalloc and GLog);
- a Windows import gets a guest address in TribesPad's import window and a
  native wrapper when TribesPad implements it, otherwise an explicit stop that
  names it;
- one dispatch table covers all modules;
- port/vengeance/vengeance_engine.c maps the images, fills the import slots,
  runs each DLL's process attach in dependency order and enters the program.

With --run it executes the program headless in a disposable clone of the game
folder and records the result or the first stop.

Usage: .venv/bin/python scripts/vengeance/build_engine.py [--target ucc] [--run [-- args ...]]
"""
import argparse, hashlib, importlib.util, json, os, pathlib, re, shutil, subprocess, sys, time
import pefile

R = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(R / 'scripts'))
LAYOUT = json.loads((R / 'config/vengeance/layout.json').read_text())['modules']
LIFT = R / 'generated/vengeance/lift'
IMPORT_BASE, IMPORT_STRIDE = 0xfffe0000, 16
TARGETS = {'ucc': ['msvcr71.dll', 'msvcp71.dll', 'ogg.dll', 'vorbis.dll', 'vorbisfile.dll', 'core.dll', 'ucc.exe'],
           # The game: every module except UCC. Those the executable does not import
           # (renderer, audio, network, GUI...) attach when the game loads them.
           'client': sorted(m for m in LAYOUT if m != 'ucc.exe')}
OPT = os.environ.get('VENGEANCE_OPT', '-O1')
GUEST_BIN = 'C:\\TribesPad\\Game\\Program\\Bin\\'
ENC = 'latin-1'  # SRW output carries raw bytes from the images

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

va = load('va', R / 'scripts/prepare-va-model.py')
bindgen = load('bindgen', R / 'scripts/gen-win32-bindings.py')
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
SDK = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
TARGET = ['-target', 'arm64-apple-macos12', '-isysroot', SDK]

# Starsiege's runtime, unchanged: TribesPad's core services plus the ones
# Starsiege added for an MSVC-era game (fast heaps, counted critical sections,
# cached case-insensitive file lookups, x87 helpers, windows, registry, sockets,
# sound and input). Its own host program (starsiege_engine.c) is not used.
ss = load('starsiege_build', R / 'scripts/starsiege/build_engine.py')
RUNTIME = list(ss.RUNTIME)
# The host: port/vengeance (C and Objective-C), with its Direct3D 9 on Metal in d3d9/.
HOST = sorted((R / 'port/vengeance').glob('**/*.c')) + sorted((R / 'port/vengeance').glob('**/*.m'))
D3D9 = R / 'port/vengeance/d3d9'
COM_BASE = 0xfff80000  # COM method addresses: clear of TribesPad's graphics (0xfffa....) and system (0xfffb....) exports
# Host routes the guest calls (cdecl), at Starsiege's addresses: SDL's allocator callbacks.
ROUTES = [(ss.HOST_BASE + 16 * i, name, helper, n) for i, (name, helper, n) in enumerate(ss.HOST_ROUTES)]
# Host window procedures the guest calls (stdcall, with the CPU): the system controls' procedure.
PROCS = [(ss.HOST_BASE + 0x100 + 16 * i, name, helper, n) for i, (name, helper, n) in enumerate([('VengeanceControlProc', 'vg_control_proc', 4)])]

def say(message):
    print('vengeance engine: ' + message, flush=True)

# Ordinal-only imports. WS2_32's numbers come from Starsiege's census; WSOCK32
# (Wine's wsock32.spec) numbers 10-12 differently: inet_addr, inet_ntoa, ioctlsocket.
_ws2 = load('census', R / 'scripts/starsiege/census.py').WS2_ORDINALS
_extra = {52: 'gethostbyname', 57: 'gethostname', 111: 'WSAGetLastError', 115: 'WSAStartup', 116: 'WSACleanup', 151: '__WSAFDIsSet'}
ORDINALS = {'ws2_32': {**_ws2, **_extra},
            'wsock32': {**_ws2, 10: 'inet_addr', 11: 'inet_ntoa', 12: 'ioctlsocket', **_extra},
            'oleaut32': {2: 'SysAllocString', 3: 'SysReAllocString', 4: 'SysAllocStringLen', 5: 'SysReAllocStringLen',
                         6: 'SysFreeString', 7: 'SysStringLen', 8: 'VariantInit', 9: 'VariantClear', 10: 'VariantCopy'},
            'comctl32': {17: 'InitCommonControls'}}

def exports_of(pe, base):
    """name/ordinal -> (address, is_code)."""
    table = getattr(pe, 'DIRECTORY_ENTRY_EXPORT', None)
    code = [(s.VirtualAddress, s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData)) for s in pe.sections if s.Characteristics & 0x20000000]
    out = {}
    for s in (table.symbols if table else []):
        assert not s.forwarder, 'forwarded export %s' % s.name
        item = (base + s.address, any(a <= s.address < b for a, b in code))
        out['#%d' % s.ordinal] = item
        if s.name:
            out[s.name.decode()] = item
    return out

def com_methods():
    """config/vengeance/com-interfaces.txt: (interface, method) in vtable order."""
    out, iface = [], None
    for line in (R / 'config/vengeance/com-interfaces.txt').read_text().splitlines():
        line = line.split('#')[0].strip()
        if line.startswith('interface '):
            iface = line.split()[1]
        elif line:
            out.append((iface, line))
    return out

def com_implemented():
    """hpcom_<Interface>_<Method> -> stack arguments with 'this' (as HaloPad's gen-com-wrappers.py finds them)."""
    found = {}
    for source in sorted(D3D9.glob('*.c')) + sorted((R / 'port/vengeance').glob('*.c')):
        text = source.read_text()
        for m in re.finditer(r'^HPCOM_FWD(\d)\((\w+),\s*(\w+),', text, re.M):
            found['hpcom_%s_%s' % (m[2], m[3])] = int(m[1]) + 1
        for m in re.finditer(r'^uint32_t (hpcom_\w+)_c\(([^)]*)\)\s*\{', text, re.M):
            params = [x for x in m[2].split(',') if x.strip() and x.strip() != 'void']
            assert all(x.strip().startswith('uint32_t') for x in params), '%s: %s takes non-uint32_t parameters' % (source.name, m[1])
            found[m[1]] = len(params)
    return found

def com_wrappers(out):
    """COM methods as stdcall services at COM_BASE + 16 * index. Each records the calling
    CPU for the Direct3D code (halopad_cpu), passes 'this' and the stack arguments to
    hpcom_<Interface>_<Method>_c, sets eax and pops them. A method without an
    implementation stops with its name. Returns the dispatch entries and counts."""
    methods, implemented = com_methods(), com_implemented()
    ll = ['; Generated by scripts/vengeance/build_engine.py: COM methods of port/vengeance/d3d9.',
          '@halopad_cpu = external thread_local global ptr', 'declare ptr @tribes_lookup(i32)',
          'declare void @vengeance_missing_com(ptr, i32) noreturn']
    entries, firsts, counts, ifaces = [], [], [], []
    for k, (iface, method) in enumerate(methods):
        if iface not in ifaces:
            ifaces.append(iface); firsts.append(k); counts.append(0)
        counts[-1] += 1
        symbol = 'vgcom_%s_%s' % (iface, method)
        entries.append((COM_BASE + 16 * k, symbol))
        n = implemented.get('hpcom_%s_%s' % (iface, method))
        if n is None:
            ll += ['define hidden fastcc void @%s(ptr %%cpu) {' % symbol, ' call void @vengeance_missing_com(ptr %%cpu, i32 %d)' % k, ' unreachable', '}']
            continue
        ll += ['declare i32 @hpcom_%s_%s_c(%s)' % (iface, method, ', '.join(['i32'] * n)),
               'define hidden fastcc void @%s(ptr %%cpu) {' % symbol, ' store ptr %cpu, ptr @halopad_cpu',
               ' %sp = getelementptr i8, ptr %cpu, i64 16', ' %esp = load i32, ptr %sp', ' %basep = getelementptr i8, ptr %cpu, i64 112',
               ' %base = load i64, ptr %basep', ' %offset = zext i32 %esp to i64', ' %addr = add i64 %base, %offset', ' %retp = inttoptr i64 %addr to ptr',
               ' %target = load i32, ptr %retp, align 1']
        for i in range(n):
            ll += [' %%argp%d = getelementptr i8, ptr %%retp, i64 %d' % (i, 4 * (i + 1)), ' %%arg%d = load i32, ptr %%argp%d, align 1' % (i, i)]
        ll += [' %%result = call i32 @hpcom_%s_%s_c(%s)' % (iface, method, ', '.join('i32 %%arg%d' % i for i in range(n))),
               ' store i32 %result, ptr %cpu', ' %%next = add i32 %%esp, %d' % (4 * (n + 1)), ' store i32 %next, ptr %sp',
               ' %fn = call ptr @tribes_lookup(i32 %target)', ' musttail call fastcc void %fn(ptr %cpu)', ' ret void', '}']
    (out / 'com.ll').write_text('\n'.join(ll) + '\n')
    c = ['/* Generated by scripts/vengeance/build_engine.py: the COM table halopad_com.c builds vtables from. */',
         '#include <stdint.h>',
         'const uint32_t halopad_com_base = %du, halopad_com_interface_count = %du;' % (COM_BASE, len(ifaces)),
         'const uint32_t halopad_com_first[] = {%s};' % ', '.join(map(str, firsts)),
         'const uint32_t halopad_com_count[] = {%s};' % ', '.join(map(str, counts)),
         'const char *const halopad_com_interfaces[] = {%s};' % ', '.join('"%s"' % i for i in ifaces),
         'const uint32_t vengeance_com_count = %du;' % len(methods),
         'const char *const vengeance_com_names[] = {%s};' % ', '.join('"%s::%s"' % m for m in methods)]
    (out / 'com_table.c').write_text('\n'.join(c) + '\n')
    return entries, sum(1 for i, m in methods if 'hpcom_%s_%s' % (i, m) in implemented), len(methods)

def order(modules, pes):
    """Process-attach order: every module after the modules it imports."""
    deps = {m: {d.dll.decode().lower() for d in getattr(pes[m], 'DIRECTORY_ENTRY_IMPORT', [])} & set(modules) for m in modules}
    done, out = set(), []
    while len(out) < len(modules):
        ready = [m for m in modules if m not in done and deps[m] <= done]
        assert ready, 'import cycle among %s' % [m for m in modules if m not in done]
        out.append(ready[0]); done.add(ready[0])
    return out

def prepare(modules, out, report, backend_bindings):
    pes, lifts = {}, {}
    for m in modules:
        lift = json.loads((LIFT / pathlib.Path(m).stem / 'result.json').read_text())
        assert lift['srw_exit'] == 0, 'run scripts/vengeance/lift.py first (%s)' % m
        lifts[m] = lift
        pes[m] = pefile.PE(str(LIFT / pathlib.Path(m).stem / lift['image']))
        assert pes[m].OPTIONAL_HEADER.ImageBase == LAYOUT[m]['base']
    exports = {m: exports_of(pes[m], LAYOUT[m]['base']) for m in modules}
    # Windows imports across all modules share one guest address each.
    windows = {}
    rows = []
    for m in modules:
        for d in getattr(pes[m], 'DIRECTORY_ENTRY_IMPORT', []):
            dll = d.dll.decode().lower()
            for imp in d.imports:
                spellings = []
                if imp.import_by_ordinal:
                    # SRW spells ordinal imports LIB_ordN or ws2_32.<name>; bind by the real name.
                    friendly = ORDINALS.get(dll.split('.')[0], {}).get(imp.ordinal)
                    raw_name = d.dll.decode().split('.')[0] + '_ord' + str(imp.ordinal)
                    name = friendly or raw_name
                    spellings = [raw_name] + (['ws2_32.' + friendly, friendly] if friendly else [])
                else:
                    name = imp.name.decode()
                rows.append(dict(module=m, dll=dll, name=name, spellings=spellings, ordinal=imp.ordinal if imp.import_by_ordinal else None, slot=imp.address))
                if dll not in modules:
                    windows.setdefault(name, dll)
    names = sorted(windows)
    wva = {n: IMPORT_BASE + IMPORT_STRIDE * i for i, n in enumerate(names)}
    assert IMPORT_BASE + IMPORT_STRIDE * len(names) < 0xfffff000
    slots, internal_code, internal_data = [], 0, 0
    procs_all = []
    for m in modules:
        work = out / pathlib.Path(m).stem
        work.mkdir(parents=True, exist_ok=True)
        lift_dir = LIFT / pathlib.Path(m).stem
        stem = pathlib.Path(lifts[m]['image']).stem
        main = (lift_dir / (stem + '.llasm')).read_text(ENC).splitlines()
        known = set(re.findall(r'^proc (\S+) external', '\n'.join(main), re.M))
        values, aliases, declare = {}, set(), set()
        for row in [r for r in rows if r['module'] == m]:
            if row['dll'] in modules:
                key = row['name'] if row['ordinal'] is None else '#%d' % row['ordinal']
                address, is_code = exports[row['dll']][key]
                value = address
                if is_code:
                    aliases.add('define %s loc_%X' % (row['name'], address)); declare.add('proc loc_%X external' % address); internal_code += 1
                else:
                    internal_data += 1
            else:
                value = wva[row['name']]
                symbol = va.symbol(row['name'])
                aliases.add('define %s %s' % (row['name'], symbol)); declare.add('proc %s external' % symbol)
            values[row['name']] = value
            for spelling in row['spellings']:
                if spelling in known and spelling != row['name']:
                    values[spelling] = value
                    aliases.add('define %s %s' % (spelling, va.symbol(row['name']) if row['dll'] not in modules else 'loc_%X' % value))
            slots.append((row['slot'], value))
        code = '\n'.join(p.read_text(ENC) for p in sorted(lift_dir.glob('seg*_code.llinc')))
        # SRW names exported procedures after their exports (malloc, memcpy, ...),
        # which would collide with the host's C library, and refers to exported
        # data by name. Use addresses for both: loc_<VA> for code, the VA for data.
        own = {}
        for name, (address, is_code) in exports[m].items():
            if name.startswith('#'):
                continue
            target = 'loc_%X' % address if is_code else hex(address)
            own[name] = target
            if not is_code:
                own['_' + name] = target
        # SRW also records every name it gave an address: "define loc_<VA> NAME".
        for line in main:
            d = re.fullmatch(r'define (loc_[0-9A-F]+) (\S+)', line)
            if d:
                own[d[2]] = d[1]
        code = re.sub(r'^proc (\S+) global c_\S+$', lambda x: 'proc ' + own.get(x[1], x[1]), code, flags=re.M)
        code = re.sub(r'[\w?@$.]+', lambda x: own.get(x[0], x[0]), code)
        named = re.findall(r'^proc ([^l]\S*|l(?!oc_)\S*)$', code, re.M)
        assert not named, '%s: named procedures left after renaming exports: %s' % (m, named[:5])
        defined = set(re.findall(r'^proc (loc_[0-9A-F]+)', code, re.M))
        # Internal imports this module defines itself never happen; drop such declarations.
        declare = {d for d in declare if not (d.startswith('proc loc_') and d.split()[1] in defined)}
        targets = set(re.findall(r'^\s*(?:tcall|ctcallz|ctcallnz)\s+.*?\b(loc_[0-9A-F]+)\s*$', code, re.M))
        lo, hi = LAYOUT[m]['base'], LAYOUT[m]['base'] + LAYOUT[m]['image_size']
        missing = sorted(t for t in targets - defined if lo <= int(t[4:], 16) < hi)
        for name in missing:
            code += '\nproc %s\ncall halopad_trap_unimplemented %s\nendp\n' % (name, hex(int(name[4:], 16)))
        foreign = sorted(t for t in targets - defined if not lo <= int(t[4:], 16) < hi)
        declare |= {'proc %s external' % t for t in foreign}
        code, stats = va.transform(code.splitlines(), values)
        (work / 'code.llinc').write_text('\n'.join(code) + '\n', ENC)
        extern = sorted(aliases) + sorted(declare) + ['funcv halopad_trap_unimplemented address', 'proc tribes_dispatch external',
                                                       'funcv tribes_set_dispatch_target $, target']
        (work / 'extern.llinc').write_text('\n'.join(extern) + '\n', ENC)
        (work / 'macros.llinc').write_text('')
        kept, skip, included = [], False, False
        for line in main:
            if re.fullmatch(r'proc (\S+) external', line) and line.split()[1] in values:
                continue
            if line.startswith('datasegment '):
                skip = True; continue
            if skip:
                skip = not line.startswith('endd'); continue
            if line.startswith('define '):
                continue
            if re.fullmatch(r'include seg\d+_code\.llinc', line):
                if not included:
                    kept.append('include code.llinc'); included = True
                continue
            if re.fullmatch(r'include seg\d+_data\.llinc', line):
                continue
            kept.append(line)
        assert included, m
        (work / 'module.llasm').write_text('\n'.join(kept) + '\n', ENC)
        key = hashlib.sha256(b''.join((work / n).read_bytes() for n in ['module.llasm', 'code.llinc', 'extern.llinc'])).hexdigest()
        cached = work / 'module.json'
        if cached.exists() and (work / 'module.ll').exists() and json.loads(cached.read_text()).get('key') == key:
            procs = json.loads(cached.read_text())['procs']
        else:
            say('%s: llasm' % m)
            with (work / 'llasm.log').open('w') as log:
                subprocess.run([str(R / 'generated/tools/llasm/llasm'), '-m64', '-ptrofs', '-O2', '-I', str(work), '-I', str(R / 'port/llasm-support'),
                                '-I', str(R / 'ref/halopad/port/llasm-support'), '-I', str(R / 'port/vengeance'), '-o', str(work / 'raw.ll'), str(work / 'module.llasm')],
                               cwd=work, stdout=log, stderr=log, check=True)
            procs, _ = va.postprocess(work / 'raw.ll', work / 'module.ll')
            (work / 'raw.ll').unlink()
            cached.write_text(json.dumps({'key': key, 'procs': procs}))
        procs_all += procs
        # The mapped, rebased image the host loads at its base.
        image = pes[m].get_memory_mapped_image()
        (out / 'images').mkdir(exist_ok=True)
        (out / 'images' / (pathlib.Path(m).stem + '.bin')).write_bytes(image + bytes(pes[m].OPTIONAL_HEADER.SizeOfImage - len(image)))
        report.setdefault('modules', {})[m] = {'procedures': len(procs), 'trapped_direct_targets': len(missing), 'foreign_targets': len(foreign),
                                              'va_stats': stats, 'untranslated_sites': len(lifts[m].get('untranslated', []))}
    # Exports used across modules must be translated procedures.
    for m in modules:
        for alias in (out / pathlib.Path(m).stem / 'extern.llinc').read_text(ENC).splitlines():
            if alias.startswith('proc loc_'):
                assert alias.split()[1] in set(procs_all), '%s needs %s, which no lift defines' % (m, alias.split()[1])
    # 0xfffff010: where SEH handlers return to the host; 0xfffff020: the dispatcher's
    # own registration handler (port/vengeance/vengeance_seh.c).
    com, com_bound, com_total = com_wrappers(out)
    report['com_methods'] = {'implemented': com_bound, 'total': com_total}
    entries = sorted([(int(n[4:], 16), n) for n in procs_all] + [(wva[n], va.symbol(n)) for n in names] + com +
                     [(address, 'tbimp_' + name) for address, name, _, _ in ROUTES + PROCS] +
                     [(0xfffff010, 'vengeance_handler_return'), (0xfffff020, 'vengeance_nested_handler')])
    (out / 'seh.ll').write_text('declare void @vg_handler_returned()\ndeclare i32 @vg_nested_handler(ptr)\ndeclare ptr @tribes_lookup(i32)\n'
                                'define hidden fastcc void @vengeance_handler_return(ptr %cpu) nounwind {\n call void @vg_handler_returned()\n ret void\n}\n'
                                'define hidden fastcc void @vengeance_nested_handler(ptr %cpu) {\n %ret = call i32 @vg_nested_handler(ptr %cpu)\n'
                                ' %fn = call ptr @tribes_lookup(i32 %ret)\n musttail call fastcc void %fn(ptr %cpu)\n ret void\n}\n')
    assert len({v for v, _ in entries}) == len(entries), 'duplicate dispatch address'
    (out / 'dispatch.ll').write_text(va.dispatch_ir(entries))
    bindings = json.loads((R / 'port/llasm-runtime/win32-bindings.json').read_text())
    for config in [R / 'config/starsiege/bindings.json', R / 'config/vengeance/bindings.json']:
        extra = json.loads(config.read_text())
        assert not set(extra['bindings']) & set(bindings), '%s: new services go in "bindings"; replacements in "overrides"' % config
        bindings.update(extra['bindings'])
        bindings.update({n: {k: v for k, v in o.items() if k != 'why'} for n, o in extra.get('overrides', {}).items()})
    # The SDL backend's wrappers (GL, DirectSound, OpenAL) come from its own files;
    # a service bound here replaces only a logged no-op entry there (as for Starsiege).
    replaced = set(bindings) & set(backend_bindings)
    assert all(backend_bindings[n]['helper'].startswith('tribes_gl_unprovided_') for n in replaced), sorted(replaced)
    wrappers = out / 'backend/graphics-bindings.ll'
    text = wrappers.read_text()
    for name in sorted(replaced):
        text, count = re.subn(r'^define hidden fastcc void @tbimp_%s\(ptr %%cpu\) \{\n.*?^\}\n' % re.escape(name), '', text, flags=re.M | re.S)
        assert count == 1, name
    wrappers.write_text(text)
    # Every wrapper: the native export table (GetProcAddress) refers to all of them.
    host = {name: {'helper': helper, 'arguments': n, 'returns': True, 'guest_base': True, 'calling_convention': 'cdecl'}
            for _, name, helper, n in ROUTES}
    host.update({name: {'helper': helper, 'arguments': n, 'returns': True, 'guest_base': False, 'cpu_argument': True}
                 for _, name, helper, n in PROCS})
    (out / 'bindings.ll').write_text(bindgen.generate({**bindings, **host}))
    used = {n for n in names if n in bindings or n in backend_bindings}
    stops = ['declare void @vengeance_missing_import(ptr, i32) noreturn']
    for i, n in enumerate(names):
        if n not in used:
            stops.append('define hidden fastcc void @%s(ptr %%cpu) {\n call void @vengeance_missing_import(ptr %%cpu, i32 %d)\n unreachable\n}' % (va.symbol(n), i))
    (out / 'stops.ll').write_text('\n'.join(stops) + '\n')
    init = order(modules, pes)
    exe = [m for m in modules if not pes[m].is_dll()]
    assert len(exe) == 1
    # Statically linked: reachable from the executable through imports.
    static, todo = set(), [exe[0]]
    while todo:
        m = todo.pop()
        if m in static: continue
        static.add(m)
        todo += [d.dll.decode().lower() for d in getattr(pes[m], 'DIRECTORY_ENTRY_IMPORT', []) if d.dll.decode().lower() in modules]
    env = json.loads((R / 'config/runtime/environment.json').read_text())
    variables = b''.join((k + '=' + v).encode(env['encoding']) + b'\0' for k, v in sorted(env['variables'].items(), key=lambda kv: kv[0].casefold())) + b'\0'
    header = lambda name, data: 'static const unsigned char %s[]={%s};' % (name, ','.join(map(str, data)))
    files = {m: pathlib.Path(LAYOUT[m]['file']).name for m in modules}
    inc = ['typedef struct {uint32_t base,size,entry,dll,dynamic;const char *image,*path;} VgModule;',
           'static const VgModule vg_modules[]={%s};' % ','.join('{%du,%du,%du,%d,%d,%s,%s}' % (
               LAYOUT[m]['base'], pes[m].OPTIONAL_HEADER.SizeOfImage, LAYOUT[m]['base'] + pes[m].OPTIONAL_HEADER.AddressOfEntryPoint,
               int(pes[m].is_dll()), int(m not in static), json.dumps(pathlib.Path(m).stem + '.bin'), json.dumps(GUEST_BIN + files[m])) for m in init),
           '#define VG_STACK_RESERVE %du' % max(pes[exe[0]].OPTIONAL_HEADER.SizeOfStackReserve, 0x10000),
           '#define VG_GUEST_BIN %s' % json.dumps(GUEST_BIN), '#define VG_EXE_NAME %s' % json.dumps(files[exe[0]]),
           '#define VG_WINDOWS_DIRECTORY ' + json.dumps(env['legacy_profile']['windows_directory']),
           '#define VG_PROFILE_FILE ' + json.dumps(env['legacy_profile']['file']),
           header('environment', variables),
           'static const uint32_t vg_imports[][2]={%s};' % ','.join('{%du,%du}' % s for s in sorted(slots)),
           'static const uint32_t sdl_allocator[4]={%s};' % ','.join('%du' % address for address, _, _, _ in ROUTES),
           '#define VG_CONTROL_PROC %du' % PROCS[0][0],
           'static const char *const import_names[]={%s};' % ','.join(json.dumps(windows[n] + '!' + n) for n in names),
           # Export tables for GetProcAddress, sorted by module base, then name (strcmp order).
           'typedef struct {uint32_t image;const char *name;uint32_t ordinal,address;} VgExport;',
           'static const VgExport vg_exports[]={%s};' % ','.join('{%du,%s,%du,%du}' % e for e in sorted(
               ((LAYOUT[m]['base'], json.dumps(n), int(o[1:]) if False else 0, a) for m in modules for n, (a, _) in exports[m].items() if not n.startswith('#')),
               key=lambda e: (e[0], json.loads(e[1]).encode()))),
           'typedef struct {uint32_t image,ordinal,address;} VgOrdinal;',
           'static const VgOrdinal vg_ordinals[]={%s};' % ','.join('{%du,%du,%du}' % (LAYOUT[m]['base'], int(n[1:]), a)
               for m in modules for n, (a, _) in sorted(exports[m].items()) if n.startswith('#'))]
    # DirectInput's keyboard and mouse: TribesPad's device catalog, as Starsiege uses it.
    # Translated DLLs a dynamically loaded DLL imports from and that are themselves
    # loaded on demand: LoadLibrary attaches them first, in attach order (Editor.dll
    # needs ScriptCompiler.dll and PerforceWrapper.dll).
    dynamic = set(modules) - static
    imports_of = {m: {d.dll.decode().lower() for d in getattr(pes[m], 'DIRECTORY_ENTRY_IMPORT', [])} & set(modules) for m in modules}
    def closure(m):
        seen, todo = set(), list(imports_of[m])
        while todo:
            d = todo.pop()
            if d not in seen:
                seen.add(d); todo += imports_of[d]
        return [x for x in init if x in seen and x in dynamic]
    pairs = [(files[m], files[d]) for m in init if m in dynamic for d in closure(m)]
    inc.append('static const struct {const char *module,*dependency;} vg_dependencies[]={%s};' %
               ','.join('{%s,%s}' % (json.dumps(a), json.dumps(b)) for a, b in pairs or [('', '')]))
    devices = json.loads((R / 'config/runtime/input-devices.json').read_text())['devices']
    cbytes = lambda h: '{' + ','.join(map(str, bytes.fromhex(h))) + '}'
    records = []
    for i, d in enumerate(devices):
        objects = ['{%s,%d,%d,%d,%s,0,0}' % (cbytes(o['guid_hex']), o['offset'], o['type'], o['flags'], json.dumps(o['name'])) for o in d['objects']]
        inc.append('static const TribesInputObject vg_input_objects_%d[]={%s};' % (i, ','.join(objects)))
        records.append('{%s,%s,%d,%s,%s,%d,%d,%d,%d,vg_input_objects_%d,%d}' % (cbytes(d['instance_guid_hex']), cbytes(d['product_guid_hex']), d['type'],
                       json.dumps(d['instance_name']), json.dumps(d['product_name']), int(d['attached']), d['axes'], d['buttons'], d['povs'], i, len(objects)))
    inc.append('static const TribesInputDevice vg_input_devices[]={%s};' % ','.join(records))
    (out / 'vengeance_engine.inc').write_text('\n'.join(inc) + '\n')
    report.update(init_order=init, dynamic=sorted(set(modules) - static), windows_imports=len(names), bound=len(used), stops=len(names) - len(used),
                  internal_code_imports=internal_code, internal_data_imports=internal_data, dispatch_entries=len(entries))
    (out / 'imports.json').write_text(json.dumps({'windows': [{'name': n, 'dll': windows[n], 'guest_va': wva[n], 'bound': n in used} for n in names]}, indent=1) + '\n')
    return modules

def compile_and_link(modules, out, report, graphics):
    math = load('sse', R / 'scripts/sse_build_inputs.py').sse_inputs('macos')
    flags = ['-O2', '-DPTROFS_64BIT', '-Wno-override-module', '-Wall', '-Wextra', '-Werror'] + math['compile_flags'] + graphics['flags']
    cache_path = out / 'objects.json'
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    objects = []
    (out / 'obj').mkdir(exist_ok=True)
    for source in RUNTIME + graphics['sources'] + HOST + [out / n for n in ['dispatch.ll', 'bindings.ll', 'stops.ll', 'seh.ll', 'com.ll', 'com_table.c']]:
        obj = out / 'obj' / (source.stem + '.o')
        key = sha(source) + (sha(out / 'vengeance_engine.inc') if source.parent.name == 'vengeance' else '') + ' '.join(flags)
        if source.parent == D3D9 or source.parent.name == 'vengeance':  # the Direct3D code's headers
            key += ''.join(sha(h) for h in sorted(D3D9.glob('*.h')))
        if cache.get(str(source)) != key or not obj.exists():
            if source.suffix == '.ll':
                cmd = ['clang'] + TARGET + ['-O2', '-Wno-override-module', '-c', str(source), '-o', str(obj)]
            else:
                # HaloPad's Direct3D code keeps HaloPad's own C dialect (scripts/run-core.py there).
                own = ['-std=c2x', '-w', '-fno-fast-math', '-ffp-contract=off'] if source.parent == D3D9 else []
                cmd = ['clang'] + TARGET + (['-fobjc-arc'] if source.suffix == '.m' else []) + flags + own + [
                    '-I', str(out), '-I', str(R / 'port/llasm-support'), '-c', str(source), '-o', str(obj)]
            subprocess.run(cmd, check=True)
            cache[str(source)] = key
        objects.append(str(obj))
    for m in modules:
        ll = out / pathlib.Path(m).stem / 'module.ll'
        obj = out / 'obj' / ('module_' + pathlib.Path(m).stem + '.o')
        key = sha(ll) + OPT
        if cache.get(str(ll)) != key or not obj.exists():
            say('%s: compiling translated code (%s)' % (m, OPT))
            start = time.monotonic()
            subprocess.run(['clang'] + TARGET + [OPT, '-Wno-override-module', '-c', str(ll), '-o', str(obj)], check=True)
            report.setdefault('compile_seconds', {})[m] = round(time.monotonic() - start)
            cache[str(ll)] = key
        objects.append(str(obj))
    cache_path.write_text(json.dumps(cache, indent=1) + '\n')
    exe = out / 'vengeance-engine'
    with (out / 'link.log').open('w') as log:
        result = subprocess.run(['clang'] + TARGET + objects + math['libraries'] + graphics['libraries'] +
                                [str(R / 'generated/bochs-math/math_bridge.o'), str(R / 'generated/bochs-math/libbochs-math.a'), '-lc++', '-lz'] +
                                [x for f in ['Foundation', 'CoreGraphics', 'CoreText', 'CoreFoundation', 'AppKit', 'Metal', 'QuartzCore',
                                 'GameController', 'AVFoundation', 'AudioToolbox', 'CoreAudio', 'SystemConfiguration', 'ImageIO', 'IOKit',
                                 'CoreHaptics', 'UniformTypeIdentifiers'] for x in ['-framework', f]] + ['-o', str(exe)], stdout=log, stderr=log)
    report['link_exit'] = result.returncode
    if result.returncode:
        report['undefined_symbols'] = sorted(set(re.findall(r'"_([^"]+)", referenced from', (out / 'link.log').read_text())))[:200]
        return None
    report['engine_sha256'] = sha(exe)
    return exe

def run(exe, out, args, timeout, report, show_script_log=False):
    work = out / 'run'
    if work.exists():
        shutil.rmtree(work)
    game, temp = work / 'volume/TribesPad/Game', work / 'volume/TribesPad/Temp'
    game.parent.mkdir(parents=True)
    # An APFS clone of the verified game: instant, and the original stays untouched.
    subprocess.run(['cp', '-cR', str(R / 'ref/vengeance/game'), str(game)], check=True)
    temp.mkdir(parents=True)
    if show_script_log:
        # Run copy only: the game's Default.ini suppresses UnrealScript's log.
        ini = game / 'Program/Bin/Default.ini'
        text = ini.read_bytes().replace(b'Suppress=ScriptLog\r\n', b'')
        ini.unlink(); ini.write_bytes(text)
    say('running %s headless' % ' '.join(args))
    try:
        result = subprocess.run([str(exe), str(out / 'images'), str(work / 'volume'), str(game), str(temp)] + args,
                                capture_output=True, text=True, errors='replace', timeout=timeout)
        stdout, stderr, code = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as error:
        stdout = (error.stdout or b'').decode(errors='replace'); stderr = (error.stderr or b'').decode(errors='replace'); code = 'timeout'
    (work / 'stdout.log').write_text(stdout)
    (work / 'stderr.log').write_text(stderr)
    lines = [l for l in stdout.splitlines() if l.startswith('{"diagnostic_stop"')]
    report['run'] = {'args': args, 'exit': code, 'result': json.loads(lines[-1]) if lines else None,
                     'stdout_tail': stdout[-2500:], 'stderr_tail': stderr[-2500:]}

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--target', choices=sorted(TARGETS), default='ucc')
    ap.add_argument('--run', action='store_true')
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--show-script-log', action='store_true', help='unsuppress ScriptLog in the run copy')
    ap.add_argument('args', nargs='*', default=['help'])
    a = ap.parse_args()
    out = R / 'generated/vengeance' / ('engine-' + a.target)
    out.mkdir(parents=True, exist_ok=True)
    report = {'target': a.target, 'playable': False, 'opt': OPT}
    ss.OUT = out  # the SDL backend's generated wrappers go in this build's folder
    graphics = ss.backend()
    modules = prepare(TARGETS[a.target], out, report, graphics['bindings'])
    exe = compile_and_link(modules, out, report, graphics)
    if exe and a.run:
        run(exe, out, a.args, a.timeout, report, a.show_script_log)
    (out / 'result.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('modules',)}, indent=1))
    return 0 if exe else 1

if __name__ == '__main__':
    sys.exit(main())
