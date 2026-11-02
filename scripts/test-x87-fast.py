#!/usr/bin/env python3
"""Check Vengeance's x87 fast paths (port/vengeance/vengeance_float80.c) against Bochs SoftFloat (tests/x87_fast_test.c)."""
import importlib.util,pathlib,subprocess,sys,tempfile
R=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sse',R/'scripts/sse_build_inputs.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
math=m.sse_inputs('macos')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
exe=pathlib.Path(tempfile.mkdtemp())/'x87-fast'
subprocess.run(['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-w',*math['compile_flags'],'-I',str(R/'port/llasm-support'),'-I',str(R/'port/vengeance'),
    str(R/'tests/x87_fast_test.c'),*math['libraries'],str(R/'generated/bochs-math/math_bridge.o'),str(R/'generated/bochs-math/libbochs-math.a'),'-lc++','-o',str(exe)],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
