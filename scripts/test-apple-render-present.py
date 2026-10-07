#!/usr/bin/env python3
"""Exercise the pinned scene completion branch and original stack epilogue."""
import ctypes, hashlib, itertools, json, os, pathlib, random, struct, subprocess, sys
from apple_render_present import translate, BODY_SHA
from console_fade_inputs import definition
from checked_support import compile_support
from guard_checked_link import guard_link

R = pathlib.Path(__file__).resolve().parents[1]
out = R / 'generated/integration-review/apple-render-present'
out.mkdir(parents=True, exist_ok=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
source = (R / 'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll').read_text()
parts = [next(s for s in source.splitlines() if s.startswith('%_cpu = type')),
         '@route = global i32 0', '@returned = global i32 0',
         'define void @tribes_set_dispatch_target(%_cpu* %cpu, i32 %pc) { store i32 %pc, i32* @returned\n ret void }',
         'define hidden fastcc void @tribes_dispatch(%_cpu* %cpu) { ret void }']
names = ['loc_10043BBC', 'loc_10043C76', 'loc_10043C7D', 'loc_10043C7E']
for prefix, text in [('original', source), ('apple', translate(source))]:
    blocks = ''.join(definition(text, name) for name in names)
    for name in names + ['loc_10043BC9']:
        blocks = blocks.replace('@' + name + '(', '@' + prefix + '_' + name + '(')
    parts += [blocks,
        f'define hidden fastcc void @{prefix}_loc_10043BC9(%_cpu* %cpu) {{ store i32 1, i32* @route\n ret void }}',
        f'define void @{prefix}(%_cpu* %cpu) {{ call fastcc void @{prefix}_loc_10043BBC(%_cpu* %cpu)\n ret void }}']
ir = out / 'blocks.ll'
ir.write_text('\n'.join(parts) + '\n')
barriers = [R / 'port/core/guest_memory_checks.c', R / 'port/core/guest_external_checks.c']
if len(sys.argv) == 1:
    objects, _ = compile_support([ir], out / 'checked', [])
    meta = json.loads((out / 'checked/result.json').read_text())
    entries = [dict(object=p, object_sha256=sha(pathlib.Path(p)), bitcode=str(pathlib.Path(p).with_suffix('.bc')),
                    bitcode_sha256=meta['objects'][pathlib.Path(p).stem]['bitcode_sha256']) for p in objects]
    guarded, _ = guard_link(entries, out / 'guarded')
    sdk = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
    for mode, inputs in [('normal', [ir]), ('checked', guarded)]:
        subprocess.run(['clang', '-target', 'arm64-apple-macos12', '-isysroot', sdk, '-O2', '-Wno-override-module',
                        '-dynamiclib', *map(str, [*barriers, *inputs]), '-o', str(out / (mode + '.dylib'))], check=True)


def exercise(mode, fault=None):
    lib = ctypes.CDLL(str(out / (mode + '.dylib')))
    native = ctypes.CDLL(None)
    native.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]
    native.mmap.restype = ctypes.c_void_p
    native.mprotect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    native.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    size = 0x100000000
    base = native.mmap(None, size, 0, 0x1002, -1, 0)
    assert base and base != ctypes.c_void_p(-1).value
    page = os.sysconf('SC_PAGE_SIZE')
    pages = sorted({v // page * page for v in [0x100000, 0x1046ce44, 0x1046e258]})
    lib.tribes_memory_checks_configure.argtypes = [ctypes.c_uint64, ctypes.c_uint64]
    assert lib.tribes_memory_checks_configure(base, size)
    lib.tribes_memory_permissions_set.argtypes = [ctypes.c_uint32, ctypes.c_uint64, ctypes.c_uint32]
    assert lib.tribes_memory_permissions_set(0, size, 0)
    for p in pages:
        assert native.mprotect(base + p, page, 3) == 0
        assert lib.tribes_memory_permissions_set(p, page, 3)
    functions = [getattr(lib, prefix) for prefix in ['original', 'apple']]
    targets = (ctypes.c_void_p * 2)(*[ctypes.cast(f, ctypes.c_void_p).value for f in functions])
    lib.tribes_memory_register_targets.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint64]
    lib.tribes_memory_register_targets(targets, 2)
    lib.tribes_memory_external_configure.argtypes = [ctypes.c_uint64, ctypes.c_uint64]
    lib.tribes_memory_external_configure(base, size)
    lib.tribes_memory_external_enable()
    for f in functions:
        f.argtypes = [ctypes.c_void_p]
    route = ctypes.c_uint32.in_dll(lib, 'route')
    returned = ctypes.c_uint32.in_dll(lib, 'returned')
    fields = [0x1046ce44, 0x1046ce48, 0x1046ce60, 0x1046e258, 0x1046ce50]
    def store(p, v):
        ctypes.memmove(base + p, struct.pack('<I', v), 4)
    rng = random.Random(25034)
    cases = 0
    for values in itertools.product([0, 1, 0x80000000, 0xffffffff], repeat=5):
        for p, v in zip(fields, values):
            store(p, v)
        stack = bytes(rng.getrandbits(8) for _ in range(128))
        ctypes.memmove(base + 0x100800, stack, len(stack))
        seed = bytearray(rng.getrandbits(8) for _ in range(440))
        struct.pack_into('<Q', seed, 112, base)
        struct.pack_into('<I', seed, 16, 0x100820)
        if fault:
            for p in fields:
                store(p, 0)
            address = {'globals': 0x1046ce44, 'current': 0x1046e258,
                       'dirty-write': 0x1046ce65, 'stack': 0x100820}[fault]
            # Guest 4K permissions isolate fields even within a native 16K page.
            assert lib.tribes_memory_permissions_set(address // 4096 * 4096, 4096, 1 if fault == 'dirty-write' else 0)
            lib.apple(ctypes.create_string_buffer(bytes(seed), 440))
            raise AssertionError('guard returned')
        results = []
        for prefix in ['original', 'apple']:
            ctypes.c_ubyte.from_address(base + 0x1046ce65).value = 0x5a
            route.value = returned.value = 0
            cpu = ctypes.create_string_buffer(bytes(seed), 440)
            getattr(lib, prefix)(cpu)
            result = (cpu.raw, route.value, returned.value, ctypes.c_ubyte.from_address(base + 0x1046ce65).value)
            assert ctypes.string_at(base + 0x100800, 128) == stack
            results.append(result)
        if any(values):
            assert results[0] == results[1], values
        else:
            # The direct-window result uses original CMP flags and the actual
            # POP/RET epilogue. Everything else in the 440-byte CPU stays intact.
            expected = bytearray(results[0][0])
            edi, esi, ebp, pc = struct.unpack_from('<4I', stack, 32)
            for offset, value in [(28, edi), (24, esi), (20, ebp), (16, 0x100834)]:
                struct.pack_into('<I', expected, offset, value)
            assert results[0][1:] == (1, 0, 0x5a)
            assert results[1] == (bytes(expected), 0, pc, 1)
        for p, v in zip(fields, values):
            assert struct.unpack('<I', ctypes.string_at(base + p, 4))[0] == v
        cases += 1
    assert native.munmap(base, size) == 0
    return cases


if len(sys.argv) > 1:
    exercise('checked', sys.argv[1])
    sys.exit(99)
report = {'passed': False}
for mode in ['normal', 'checked']:
    cases = exercise(mode)
    report[mode] = dict(cases=cases, binary_sha256=sha(out / (mode + '.dylib')))
    print('PASS', mode, cases, 'original branch/CPU/stack comparisons', flush=True)
for fault in ['globals', 'current', 'dirty-write', 'stack']:
    result = subprocess.run([sys.executable, __file__, fault], capture_output=True, text=True, timeout=30)
    (out / (fault + '.log')).write_text(result.stdout + result.stderr)
    assert result.returncode == -6 and 'memory violation' in result.stderr, (fault, result.stderr)
report.update(passed=True, checked_faults=4,
    original_blocks_sha256={n: hashlib.sha256(definition(source, n).encode()).hexdigest() for n in names},
    sources_sha256={str(p.relative_to(R)): sha(p) for p in [pathlib.Path(__file__).resolve(), R / 'scripts/apple_render_present.py', *barriers]},
    scope='Original branch and epilogue, 1024 state combinations per mode; full CPU/stack and nonzero target routing preserved. No native GL or playable acceptance claimed.')
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
