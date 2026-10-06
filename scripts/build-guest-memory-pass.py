#!/usr/bin/env python3
"""Build an isolated LLVM20 build-time permission pass from installed tools."""
import hashlib,json,pathlib,shlex,subprocess
R=pathlib.Path(__file__).resolve().parents[1];prefix=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8');out=R/'generated/tools/guest-memory-pass';out.mkdir(parents=True,exist_ok=True)
config=prefix/'bin/llvm-config';version=subprocess.check_output([str(config),'--version'],text=True).strip();assert version=='20.1.8'
source=R/'port/compiler/GuestMemoryPass.cpp';plugin=out/'GuestMemoryPass.dylib';flags=shlex.split(subprocess.check_output([str(config),'--cxxflags','--ldflags','--libs','core','passes','--system-libs'],text=True))
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
cmd=[str(prefix/'bin/clang++'),'-isysroot',sdk,'-dynamiclib','-fPIC','-O2',str(source)]+flags+['-Wl,-rpath,'+str(prefix/'lib'),'-o',str(plugin)]
run=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();(out/'result.json').write_text(json.dumps({'state':'built','version':version,'source_sha256':sha(source),'plugin_sha256':sha(plugin),'tools_sha256':{str(prefix/'bin'/n):sha(prefix/'bin'/n) for n in ['opt','clang','clang++','llvm-config']},'command':cmd},indent=2)+'\n');print(plugin)
