#!/usr/bin/env python3
"""Verify native CRC fold-state bytes and direct CPU shim on macOS ARM64."""
import ctypes,hashlib,json,pathlib,struct,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
reference=R/'generated/qol-crc-final-reference/result.json';r=json.loads(reference.read_text());assert r['pass']
for name,h in r['source_sha256'].items():assert sha(R/name)==h
out=R/'generated/zlib-ng/macos/crc-final-buffer';out.mkdir(exist_ok=True);build=R/'generated/zlib-ng/macos/build'
source=R/'port/core/guest_zlib.c';exe=out/'native.dylib';sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
cmd=['clang','-dynamiclib','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-I',str(build),'-I',str(R/'ref/zlib-ng'),str(source),str(build/'libz-ng.a'),'-o',str(exe)]
subprocess.run(cmd,check=True);lib=ctypes.CDLL(str(exe));lib.tribes_zlib_crc_fold_final.argtypes=[ctypes.c_void_p];lib.tribes_zlib_crc_fold_final.restype=ctypes.c_uint32;lib.tribes_zlib_crc_fold_final_cpu.argtypes=[ctypes.c_void_p,ctypes.c_uint32];lib.tribes_zlib_crc_fold_final_cpu.restype=ctypes.c_uint32
count=0
for alignment in range(16):
 for case in r['cases']:
  address=17+alignment;initial=b'\xa6'*address+bytes.fromhex(case['fold_hex'])+struct.pack('<I',case['previous_value'])+b'\xa6'*32
  wanted=initial[:address+64]+struct.pack('<I',case['result'])+initial[address+68:]
  for cpu_call in [False,True]:
   arena=ctypes.create_string_buffer(initial,len(initial));cpu=ctypes.create_string_buffer(bytes([0xa5])*440,440)
   ctypes.memmove(ctypes.addressof(cpu)+112,struct.pack('<Q',ctypes.addressof(arena)),8)
   expected_cpu=cpu.raw[:312]+bytes(80)+cpu.raw[392:]
   result=lib.tribes_zlib_crc_fold_final_cpu(cpu,address) if cpu_call else lib.tribes_zlib_crc_fold_final(ctypes.addressof(arena)+address)
   assert result==case['result'] and arena.raw==wanted
   if cpu_call:assert cpu.raw==expected_cpu
   count+=1
paths=[source,R/'port/core/guest_zlib.h',R/'port/llasm-support/llasm_cpu.h',pathlib.Path(__file__).resolve()]
report={'pass':True,'cases':count,'original_reference_sha256':sha(reference),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'binary_sha256':sha(exe),'compile_command':cmd,'scope':'771 original offline fold-final outcomes at16 alignments through direct native buffer and CPU shim calls on macOS ARM64; complete state/guards and440-byte CPU contract. No original-PC dispatch/cdecl wrapper, simulator execution, actual-input CRC, hardwareEVEX, full module or gameplay proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',count,'native CRC final buffer/CPU calls')
