#!/usr/bin/env python3
"""Package the existing checked full engine for a direct Finder launch."""
import argparse,hashlib,json,pathlib,plistlib,shutil,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--bootstrap-directory',type=pathlib.Path,default=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe');a=ap.parse_args()
source=a.bootstrap_directory.resolve();out=R/'generated/integration-review/macos-engine-app';app=out/'TribesPad.app';bin=app/'Contents/MacOS';bin.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
meta=json.loads((source/'result.json').read_text());assert meta['state']=='finished' and meta['independent_subpages_enabled'];assert sha(source/'bootstrap')==meta['binary_sha256']
game_manifest=R/'generated/apple-game-profile.json'
game_profile=json.loads(game_manifest.read_text())
for row in game_profile['overrides']:assert sha(R/game_profile['root']/row['path'])==row['sha256']
image_profile=None
if (source/'apple-legacy-gl-profile.json').exists():
 image_profile=json.loads((source/'apple-legacy-gl-profile.json').read_text());assert sha(source/'qol.bin')==image_profile['mapped_after_sha256']
 assert sha(R/'scripts/apple_legacy_gl_profile.py')==image_profile['source_sha256']
engine=bin/'TribesPadEngine';shutil.copy2(source/'bootstrap',engine)
args=[str(engine),str(source/'engine.bin'),str(source/'qol.bin'),str(source/'volume'),str(R/'generated/apple-game-profile'),str(source/'volume/TribesPad/Temp'),str(source),'engine']
assert all(pathlib.Path(p).exists() for p in args[:-1])
header=out/'macos-engine-launch-config.h'
header.write_text('static const char *launch_workdir='+json.dumps(str(R))+';\nstatic char *launch_arguments[]={'+','.join(json.dumps(p) for p in args)+',NULL};\n'+''.join('static const char *launch_'+name+'='+json.dumps(str(out/file))+';\n' for name,file in [('stdout','app-stdout.log'),('stderr','app-stderr.log'),('pid','app.pid')]))
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
subprocess.run(['xcrun','clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-Wall','-Wextra','-Werror','-I',str(out),str(R/'port/apple/macos_engine_launcher.c'),'-o',str(bin/'TribesPad')],check=True)
(app/'Contents/Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='com.tribespad.original-engine',CFBundleExecutable='TribesPad',CFBundleName='TribesPad',CFBundleDisplayName='TribesPad',CFBundlePackageType='APPL',CFBundleVersion='1',CFBundleShortVersionString='0.1',LSMinimumSystemVersion='12.0',NSHighResolutionCapable=True,NSMicrophoneUsageDescription='TribesPad uses microphone input for the original Tribes 2 voice chat.')))
subprocess.run(['codesign','--force','--sign','-',str(engine)],check=True)
subprocess.run(['codesign','--force','--sign','-',str(app)],check=True)
subprocess.run(['codesign','--verify','--strict','--verbose=2',str(app)],check=True)
(out/'app-build.json').write_text(json.dumps(dict(app=str(app),original_binary_sha256=meta['binary_sha256'],signed_engine_sha256=sha(engine),launcher_sha256=sha(bin/'TribesPad'),launcher_source_sha256=sha(R/'port/apple/macos_engine_launcher.c'),arguments=args,image_sha256={name:sha(source/name) for name in ['engine.bin','qol.bin']},apple_legacy_gl_profile=image_profile,game_profile=dict(manifest_sha256=sha(game_manifest),overrides=game_profile['overrides']),scope='Finder-launchable development app using the complete checked engine and existing workspace assets. Packaging is not gameplay acceptance.'),indent=2)+'\n')
print(app)
