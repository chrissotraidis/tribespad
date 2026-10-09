#!/usr/bin/env python3
import argparse,hashlib,json,pathlib,subprocess
ap=argparse.ArgumentParser();ap.add_argument("--mobile",action="store_true");args=ap.parse_args()
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review'/('code-revision-mobile' if args.mobile else 'code-revision');out.mkdir(exist_ok=True)
source=R/'tests/published_code_store.c';objects,build_sha=compile_support([source],out/'checked',[])
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
sources=[R/'tests/code_revision_test.c',R/'port/core/guest_memory_checks.c'];exe=out/'test'
subprocess.run(['clang','-isysroot',sdk,'-target','arm64-apple-macos12','-O2','-Wall','-Wextra','-Werror',*(['-DTB_MOBILE_LEGACY_FONT_BANK'] if args.mobile else []),*map(str,sources),*objects,'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr)
assert run.returncode==0 and run.stderr.count('write to published AOT code')==109,(run.returncode,run.stdout,run.stderr)
report=dict(passed=True,mobile=args.mobile,renderer_bank_entries=18 if args.mobile else 26,checked_store_build_sha256=build_sha,binary_sha256=sha(exe),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[source,R/'port/core/guest_memory_checks.h',pathlib.Path(__file__).resolve()]},scope='Thirty-three primary plus two independent font ordered single-owner finite code revisions, including chained overlapping preimages and last-writer final-byte validation; rejected requests preserve bytes/state and ordinary compiler-instrumented writes remain blocked before, during and after revision. The pending, platform-specific renderer and audio bank additions validate bytes, range, permissions and owner; immutable bank instructions and mutable metadata remain distinct. No engine route activation claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout,end='')
