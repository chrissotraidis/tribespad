#!/usr/bin/env python3
"""Verify private CRC bulk buffer and CPU seed boundary on macOS ARM64."""
import ctypes,hashlib,json,pathlib,struct,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
reference=R/'generated/qol-crc-bulk-reference/result.json';r=json.loads(reference.read_text());assert r['pass']
for name,h in r['source_sha256'].items():assert sha(R/name)==h
out=R/'generated/zlib-ng/macos/crc-bulk-buffer';out.mkdir(exist_ok=True)
build=R/'generated/zlib-ng/macos/build';source=R/'port/core/guest_zlib.c';exe=out/'native.dylib'
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
cmd=['clang','-dynamiclib','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-I',str(build),'-I',str(R/'ref/zlib-ng'),str(source),str(build/'libz-ng.a'),'-o',str(exe)]
subprocess.run(cmd,check=True);lib=ctypes.CDLL(str(exe))
lib.tribes_zlib_crc_fold_bulk.argtypes=[ctypes.POINTER(ctypes.c_void_p),ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32];lib.tribes_zlib_crc_fold_bulk.restype=ctypes.c_uint32
lib.tribes_zlib_crc_fold_bulk_cpu.argtypes=[ctypes.c_void_p]+[ctypes.c_uint32]*7;lib.tribes_zlib_crc_fold_bulk_cpu.restype=ctypes.c_uint32
count=0
for alignment in range(16):
 for case in r['cases']:
  offsets=[17+alignment,66+alignment,115+alignment,164+alignment];address=512+alignment
  data=bytes(case['length']) if case['pattern']==0 else (bytes([255])*case['length'] if case['pattern']==1 else bytes((j*17+case['length']*3)&255 for j in range(case['length'])))
  initial=bytearray(b'\xa6'*(address+len(data)+32));initial[address:address+len(data)]=data
  state=bytes.fromhex(case['state_hex']);seed=bytes.fromhex(case['initial_hex']);folded=bytes.fromhex(case['result_hex'])
  for lane,offset in enumerate(offsets):initial[offset:offset+16]=state[lane*16:lane*16+16]
  wanted=bytearray(initial)
  for lane,offset in enumerate(offsets):wanted[offset:offset+16]=folded[lane*16:lane*16+16]
  for cpu_call in [False,True]:
   arena=ctypes.create_string_buffer(bytes(initial),len(initial));cpu=ctypes.create_string_buffer(bytes([0xa5])*440,440);base=ctypes.addressof(arena)
   ctypes.memmove(ctypes.addressof(cpu)+112,struct.pack('<Q',base),8);ctypes.memmove(ctypes.addressof(cpu)+176,seed,16)
   expected_cpu=cpu.raw[:312]+bytes(128)
   if cpu_call:result=lib.tribes_zlib_crc_fold_bulk_cpu(cpu,*offsets,address,case['length'],case['first'])
   else:
    pointers=(ctypes.c_void_p*4)(*(base+offset for offset in offsets));seed_buffer=ctypes.create_string_buffer(seed,16)
    result=lib.tribes_zlib_crc_fold_bulk(pointers,base+address,case['length'],seed_buffer,case['first'])
   assert result==case['processed'] and arena.raw==bytes(wanted),(alignment,case,cpu_call)
   if cpu_call:assert cpu.raw==expected_cpu
   count+=1
paths=[source,R/'port/core/guest_zlib.h',R/'port/llasm-support/llasm_cpu.h',pathlib.Path(__file__).resolve()]
report={'pass':True,'cases':count,'original_reference_sha256':sha(reference),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'binary_sha256':sha(exe),'compile_command':cmd,'scope':'694 original private bulk outcomes at16 alignments through direct native buffer and CPU shim on macOS ARM64. Seven stack arguments represented by direct calls plus XMM0 seed; complete source/state arena and440-byte CPU contract. Length>=256/disjoint arenas. No protected-page, original-PC/cdecl dispatch, simulator/device execution, physical EVEX or full client proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',count,'native CRC bulk buffer/CPU calls')
