#!/usr/bin/env python3
"""Apply verified Tribes extensions to the separate, pinned HaloPad SRW copy."""
import hashlib,json,pathlib,shutil,subprocess,sys
R=pathlib.Path(__file__).resolve().parents[1];base=R/'generated/tools/SRW';dst=R/'generated/tools/SRW-tribes';patch=R/'scripts/patches/srw-tribes.patch'
lock=json.loads((R/'toolchains.lock.json').read_text())['tribes_extensions']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(patch)==lock['patch_sha256'],'patch identity mismatch'
for f,h in lock['base_sources'].items():assert sha(base/f)==h,'base source mismatch: '+f
import SCons
assert SCons.__version__==lock['scons']
# Recreate only compiler sources affected by this patch, preserving build objects.
if not dst.exists():shutil.copytree(base,dst)
for f in lock['base_sources']:shutil.copy2(base/f,dst/f)
subprocess.run(['patch','--batch','-p1','-i',str(patch)],cwd=dst,check=True)
subprocess.run([sys.executable,'-m','SCons','-j','4'],cwd=dst,check=True)
manifest={'base_sources':lock['base_sources'],'patch_sha256':sha(patch),'srw_sha256':sha(dst/'SRW.exe'),'clang':subprocess.check_output(['clang','--version'],text=True),'sdk':subprocess.check_output(['xcrun','--show-sdk-build-version'],text=True).strip()}
(dst/'tribes-build.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
