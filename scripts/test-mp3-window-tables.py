#!/usr/bin/env python3
"""Run original MP3 window loops and check their sine tables, including old failure."""
import ctypes, hashlib, json, math, mmap, pathlib, re, struct, subprocess
from x87_store_inputs import R, DEFINITION, mp3_changes
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
old, new, selected = mp3_changes()
out = R / 'generated/integration-review/mp3-window-tables'; out.mkdir(exist_ok=True)
sdk = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
sources = [R / n for n in ['port/core/ext80.c', 'port/llasm-support/tribes_float80.c', 'tests/x87_result_slot.c']]
loops = [('loc_26F02290', 'loc_26F022B3', 36, 0x26f2283c), ('loc_26F022C0', 'loc_26F022E3', 18, 0x26f228cc)]
results = {}
for label, module in [('old', old), ('fixed', new)]:
    text = module.read_text()
    definitions = {m[1]: m[0] for m in DEFINITION.finditer(text)}
    header = text[:next(DEFINITION.finditer(text)).start()]
    code = header
    for i, (name, end, count, dest) in enumerate(loops):
        code += definitions[name]
        code += 'define hidden fastcc void @%s(%%_cpu* %%cpu) { ret void }\n' % end
        code += 'define ccc void @window_%d(%%_cpu* %%cpu) { call fastcc void @%s(%%_cpu* %%cpu)\nret void }\n' % (i, name)
    ir = out / (label + '.ll'); ir.write_text(code)
    dylib = out / (label + '.dylib')
    subprocess.run(['clang', '-arch', 'arm64', '-isysroot', sdk, '-O2', '-dynamiclib', '-Wno-override-module', '-DPTROFS_64BIT', '-DSOFTFLOAT_FAST_INT64', '-DTHREAD_LOCAL=_Thread_local', '-I', str(R/'ref/softfloat/SoftFloat-3e/source/include'), str(ir), *map(str, sources), str(R/'generated/softfloat/macos/softfloat.a'), str(R/'generated/bochs-math/libmath.dylib'), '-o', str(dylib)], check=True)
    lib = ctypes.CDLL(str(dylib)); lib.x87_fninit_void.argtypes = [ctypes.c_void_p]
    memory = mmap.mmap(-1, 0x27000000)
    base = ctypes.addressof(ctypes.c_char.from_buffer(memory))
    data = module.with_name('image.bin').read_bytes(); memory[0x26f00000:0x26f00000+len(data)] = data
    add = struct.unpack_from('<f', memory, 0x26f157a8)[0]
    mul = struct.unpack_from('<f', memory, 0x26f157a4)[0]
    matched = 0; captured = []
    for i, (name, end, count, dest) in enumerate(loops):
        cpu = ctypes.create_string_buffer(440); lib.x87_fninit_void(cpu)
        struct.pack_into('<I', cpu, 16, 0x10000)
        struct.pack_into('<Q', cpu, 112, base)
        struct.pack_into('<i', memory, 0x10000, 0)
        memory[dest:dest+4*(count+2)] = b'\xa5' * (4*(count+2))
        fn = getattr(lib, 'window_'+str(i)); fn.argtypes = [ctypes.c_void_p]; fn(cpu)
        assert struct.unpack_from('<I', cpu, 0)[0] == count
        assert struct.unpack_from('<I', memory, 0x10000)[0] == count
        assert struct.unpack_from('<I', cpu, 36)[0] == 0
        assert memory[dest:dest+4] == b'\xa5'*4
        assert memory[dest+4*(count+1):dest+4*(count+2)] == b'\xa5'*4
        actual = struct.unpack_from('<'+'f'*count, memory, dest+4)
        expected = [struct.unpack('<f', struct.pack('<f', math.sin((n+add)*mul)))[0] for n in range(count)]
        matches = sum(abs(a-b) <= 1e-7 for a,b in zip(actual,expected))
        matched += matches; captured.append(dict(function=name, values=list(actual), expected=expected, matches=matches))
    results[label] = dict(matches=matched, tables=captured, library_sha256=sha(dylib))
    memory.close()
assert results['old']['matches'] == 0 and results['fixed']['matches'] == 54, results
report = dict(passed=True, values=54, module_sha256=sha(new), old_module_sha256=sha(old), results=results,
              source_sha256={str(p.relative_to(R)):sha(p) for p in sources+[pathlib.Path(__file__).resolve(), R/'scripts/x87_store_inputs.py']},
              scope='Original native MP3 table loops, original constants, all 54 float values checked against sine, loop exits and destination canaries. Old module fails all values. Not complete MP3 output or audible playback acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 54 original MP3 window values; old module fails all 54; loop exits and canaries intact')
