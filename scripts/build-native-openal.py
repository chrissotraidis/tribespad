#!/usr/bin/env python3
"""Build the pinned native OpenAL Soft mixer for the Mac game provider."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
revision='dc7d7054a5b4f3bec1dc23a42fd616a0847af948'
src=R/'ref/openal-soft';out=R/'generated/openal-soft/macos';out.mkdir(parents=True,exist_ok=True)
if not src.exists():subprocess.run(['git','clone','--depth','1','--branch','1.24.3','https://github.com/kcat/openal-soft.git',str(src)],check=True)
assert subprocess.check_output(['git','-C',str(src),'rev-parse','HEAD'],text=True).strip()==revision
assert not subprocess.check_output(['git','-C',str(src),'status','--porcelain'],text=True).strip(),'Modified pinned OpenAL source'
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
command=['cmake','-S',str(src),'-B',str(out),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0','-DCMAKE_OSX_SYSROOT='+sdk,'-DLIBTYPE=STATIC','-DALSOFT_UTILS=OFF','-DALSOFT_EXAMPLES=OFF','-DALSOFT_TESTS=OFF','-DALSOFT_INSTALL=OFF','-DALSOFT_REQUIRE_COREAUDIO=ON']
for name in ['PIPEWIRE','PULSEAUDIO','JACK','PORTAUDIO','SDL2','SDL3','WAVE','OPENSL','OBOE']:command+=['-DALSOFT_BACKEND_'+name+'=OFF']
subprocess.run(command,check=True)
subprocess.run(['cmake','--build',str(out),'--target','OpenAL','--parallel','4'],check=True)
library=out/'libopenal.a';assert library.exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=dict(state='built',revision=revision,library=str(library.relative_to(R)),library_sha256=sha(library),architecture='arm64',sdk=sdk,configure=command,scope='Native static mixer with required CoreAudio backend. Build only; original game integration and Apple platform acceptance remain separate.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
