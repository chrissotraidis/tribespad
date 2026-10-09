#!/usr/bin/env python3
"""Verify checked-object reuse and invalidation, including executed new code."""
import ctypes,hashlib,json,pathlib,subprocess,tempfile
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
root=R/'generated/integration-review/checked-build-cache';root.mkdir(exist_ok=True)
out=pathlib.Path(tempfile.mkdtemp(prefix='run-',dir=root));source=out/'fixture.ll'
def write(value):source.write_text('define void @cache_fixture(ptr %p) { store i32 '+str(value)+', ptr %p\n ret void\n}\n')
def support():
 objects,_=compile_support([source],out/'support',[])
 report=json.loads((out/'support/result.json').read_text());row=report['objects']['00-fixture'];p=pathlib.Path(objects[0])
 entry=dict(object=str(p),object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256'])
 return entry,bool(row.get('reused_verified_llvm'))
def guard(entry):
 objects,_=guard_link([entry],out/'guarded');report=json.loads((out/'guarded/result.json').read_text());return pathlib.Path(objects[0]),bool(report['objects'][0].get('reused_verified_bitcode'))
write(17);first,reused=support();assert not reused;again,reused=support();assert reused and first==again
obj,reused=guard(first);assert not reused;_,reused=guard(first);assert reused
# Changing the backend must regenerate native code without repeating the checked pass.
receipt=out/'guarded/result.json';state=json.loads(receipt.read_text())
checked=pathlib.Path(state['objects'][0]['object']).with_suffix('.bc');checked_stamp=checked.stat().st_mtime_ns
state['codegen']['flags']=['-O0'];receipt.write_text(json.dumps(state))
_,reused=guard(first);assert not reused
state=json.loads(receipt.read_text());assert state['objects'][0]['reused_checked_ir'] and checked.stat().st_mtime_ns==checked_stamp
obj.write_bytes(b'corrupted native object');_,reused=guard(first);assert not reused
state=json.loads(receipt.read_text());assert state['objects'][0]['reused_checked_ir'] and checked.stat().st_mtime_ns==checked_stamp
write(29);changed,reused=support();assert not reused and changed['object_sha256']!=first['object_sha256'];obj,reused=guard(changed);assert not reused
library=out/'changed.dylib';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-dynamiclib',str(obj),str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c'),'-o',str(library)],check=True)
lib=ctypes.CDLL(str(library));lib.cache_fixture.argtypes=[ctypes.c_void_p];value=ctypes.c_uint32();lib.cache_fixture(ctypes.byref(value));assert value.value==29
pathlib.Path(changed['bitcode']).write_bytes(b'corrupted cached bitcode');repaired,reused=support();assert not reused and repaired==changed
policy=out/'guarded/result.json';state=json.loads(policy.read_text());state['policy_sha256']='invalid';policy.write_text(json.dumps(state));stamp=obj.stat().st_mtime_ns;_,reused=guard(repaired);assert not reused
state=json.loads(policy.read_text());assert state['objects'][0]['reused_native_after_guard'] and obj.stat().st_mtime_ns==stamp
# Expand the actual checked link set: the guard policy changes, while the
# original fixture's guarded bytes and native object remain identical.
extra=out/'extra.ll';extra.write_text('define void @unrelated_export() { ret void }\n')
objects,_=compile_support([extra],out/'extra',[]);p=pathlib.Path(objects[0]);extra_entry=dict(object=str(p),object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=sha(p.with_suffix('.bc')))
before=state['policy_sha256'];guard_link([repaired,extra_entry],out/'guarded');state=json.loads(policy.read_text())
assert state['policy_sha256']!=before and state['objects'][0]['reused_native_after_guard'] and obj.stat().st_mtime_ns==stamp
report=dict(passed=True,run=str(out.relative_to(R)),changed_value=value.value,binary_sha256=sha(library),script_sha256=sha(pathlib.Path(__file__).resolve()),builders_sha256={str(p.relative_to(R)):sha(p) for p in [R/'scripts/checked_codegen.py',R/'scripts/checked_support.py',R/'scripts/guard_checked_link.py']},scope='Exact source/tool/bitcode/object cache hit; source change rebuilds both passes and executes the new store; Backend identity changes and corrupted native objects rebuild from verified checked IR; corrupted bitcode and policy mismatch rerun the guard pass. A changed checked-symbol set reuses native code only after fresh guarded bitcode is byte-identical and compiler/object hashes match. C and Objective-C inputs are never reused by this cache.')
(root/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: checked cache reuse, fresh policy validation with identical-IR native reuse, source/bitcode invalidation, executed rebuilt value 29')
