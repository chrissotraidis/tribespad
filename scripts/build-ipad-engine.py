#!/usr/bin/env python3
"""Build the full checked original engine for iPad Simulator or device.

Run test-qol-native-bootstrap.py --enter-engine --probe-ntdll --prepare-only first.
Device builds bundle assets and require Apple development signing before installation.
"""
import argparse,hashlib,importlib.util,json,os,pathlib,plistlib,re,shutil,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
from checked_bochs_inputs import checked_bochs_inputs
from sdl_backend_inputs import inputs as sdl_inputs
from gl4es_backend_inputs import inputs as gl4es_inputs
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--platform',choices=['simulator','device'],default='simulator')
ap.add_argument('--simulator-device')
ap.add_argument('--angle',action='store_true',help='Use the pinned ANGLE Metal renderer')
ap.add_argument('--cpu-host-checks',action='store_true',help='Validate native CPU spans and prove bounded native-object accesses; keep guest accesses checked')
ap.add_argument('--hardware-permissions',action='store_true',help='MMU-enforced checked memory: exact 4 KiB checks through page protections and fault emulation instead of per-access brackets')
ap.add_argument('--bundle-assets',action='store_true',help='Bundle game data (always enabled for device)')
ap.add_argument('--prepared-directory',type=pathlib.Path,default=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe')
ap.add_argument('--build-only',action='store_true')
a=ap.parse_args()
if a.platform=='device' and not a.build_only:ap.error('Device builds require --build-only; sign and install with your Apple development identity')
if a.platform=='simulator' and not a.build_only and not a.simulator_device:ap.error('--simulator-device is required to launch')
bundled=a.bundle_assets or a.platform=='device'
if a.hardware_permissions and a.cpu_host_checks:ap.error('--hardware-permissions replaces --cpu-host-checks')
source=a.prepared_directory.resolve();out=R/(('generated/ipad-engine-device' if a.platform=='device' else 'generated/ipad-engine')+('-hw' if a.hardware_permissions else ''));out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if a.cpu_host_checks:
 for proof_name in ['cpu-host-checks','host-local-checks','native-cpu-checks','sse-host-locals','x87-host-checks']:
  cpu_proof=json.loads((R/'generated/integration-review'/proof_name/'result.json').read_text());assert cpu_proof['passed']
  for name,h in cpu_proof['sources_sha256'].items():assert sha(R/name)==h,name
bochs,_=checked_bochs_inputs(a.platform,a.hardware_permissions)
if a.platform=='simulator' and not a.build_only:
 devices=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True))
 booted=[d for rows in devices['devices'].values() for d in rows if d['state']=='Booted']
 assert len(booted)==1 and booted[0]['udid']==a.simulator_device and 'iPad' in booted[0]['deviceTypeIdentifier']
prepared=json.loads((source/'prepared-inputs.json').read_text());assert prepared['state']=='prepared'
# Original translated sources must match preparation exactly. Native service
# sources are rebuilt below, so edits to those do not repeat image preparation.
for name,h in prepared['sources_sha256'].items():
 if pathlib.Path(name).suffix=='.ll':assert sha(R/name)==h,name
for name,h in prepared['images'].items():assert sha(source/name)==h,name
sources=[R/p for p in prepared['sources']]
from mobile_gpu_timing import translate as mobile_timing, BLOCKS as timing_blocks
timing_proof=R/'generated/integration-review/mobile-gpu-timing/result.json'
timing_test=json.loads(timing_proof.read_text());assert timing_test['passed']
for name,h in timing_test['sources_sha256'].items():assert sha(R/name)==h,name
mobile_inputs=out/'mobile-inputs';mobile_inputs.mkdir(exist_ok=True)
qol_sources=[p for p in sources if p.name=='qol-patched.ll'];assert len(qol_sources)==1
from mobile_gpu_sync import prepare as mobile_sync
mobile_qol=mobile_inputs/'qol-patched.ll';mobile_qol.write_text(mobile_sync(mobile_timing(qol_sources[0].read_text())))
sources[sources.index(qol_sources[0])]=mobile_qol
from mobile_renderer_hooks import prepare as mobile_hooks
hook_proof=R/'generated/integration-review/code-revision-mobile/result.json'
hook_test=json.loads(hook_proof.read_text());assert hook_test['passed'] and hook_test['mobile']
for name,h in hook_test['sources_sha256'].items():assert sha(R/name)==h,name
sources,hook_layout=mobile_hooks(sources,mobile_inputs)
from x87_store_inputs import prepare as x87_store_inputs
sources=x87_store_inputs(sources,mobile_inputs)
# Replace the reached WAV and SDL error import stops. Keep the frozen preparation
# immutable and run the usual memory/external-call passes over these wrappers.
wave_proof_path=R/'generated/integration-review/sdl-io/result.json'
wave_proof=json.loads(wave_proof_path.read_text());assert wave_proof['passed']
for name,h in wave_proof['sources_sha256'].items():assert sha(R/name)==h,name
wave_names=['SDL_IOFromConstMem','SDL_LoadWAV_IO','SDL_ClearError']
wave_bindings=json.loads((R/'config/runtime/sdl-io-bindings.json').read_text())
generator_spec=importlib.util.spec_from_file_location('wave_bindings',R/'scripts/gen-win32-bindings.py')
generator=importlib.util.module_from_spec(generator_spec);generator_spec.loader.exec_module(generator)
wave_wrapper=mobile_inputs/'sdl-wav-bindings.ll'
wave_wrapper.write_text(generator.generate({n:wave_bindings[n] for n in wave_names},False))
old_stops=next(p for p in sources if p.name=='stops.ll');stops_text=old_stops.read_text()
for name,ordinal in zip(wave_names,[257,256,347]):
    body='define hidden fastcc void @tbimp_'+name+'(ptr %cpu) { call void @qol_missing_import(ptr %cpu, i32 '+str(ordinal)+')\n unreachable\n}'
    assert stops_text.count(body)==1,name
    stops_text=stops_text.replace(body,'',1)
# The frozen bootstrap stopped at ExitProcess. Replace only that reached import
# after native/checked/TSan shutdown through the emitted guest ABI passes.
shutdown_proof=json.loads((R/'generated/integration-review/guest-threads/macos/result.json').read_text())
assert shutdown_proof['passed'] and all(row.get('process_exit_codes')==[0,23] for row in shutdown_proof['runs'])
for name,h in shutdown_proof['sources_sha256'].items():assert sha(R/name)==h,name
exit_bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text())
exit_wrapper=mobile_inputs/'process-exit-bindings.ll'
exit_wrapper.write_text(generator.generate({n:exit_bindings[n] for n in ['ExitProcess','InterlockedFlushSList']},False))
exit_stop='define hidden fastcc void @tbimp_ExitProcess(ptr %cpu) { call void @qol_missing_import(ptr %cpu, i32 175)\n unreachable\n}'
assert stops_text.count(exit_stop)==1
stops_text=stops_text.replace(exit_stop,'',1);sources.append(exit_wrapper)
flush_proof=json.loads((R/'generated/native-win32/slist-abi/macos/result.json').read_text())
assert flush_proof['pass'] and {r['mode'] for r in flush_proof['runs']}=={'normal','checked','tsan'}
for name,h in flush_proof['sources_sha256'].items():assert sha(R/name)==h,name
assert flush_proof['bindings_sha256']==sha(R/'port/llasm-runtime/win32-bindings.json')
flush_stop='define hidden fastcc void @tbimp_InterlockedFlushSList(ptr %cpu) { call void @qol_missing_import(ptr %cpu, i32 186)\n unreachable\n}'
assert stops_text.count(flush_stop)==1
stops_text=stops_text.replace(flush_stop,'',1)
wave_stops=mobile_inputs/'stops.ll';wave_stops.write_text(stops_text)
# First-run TribesNEXT downloads write, inspect and rename files. Replace only
# those reached import stops with native file services (guest_file.c).
first_run=json.loads((R/'config/runtime/first-run-file-bindings.json').read_text())
info_wrapper=mobile_inputs/'first-run-file-bindings.ll'
info_wrapper.write_text(generator.generate(first_run['bindings'],False))
for name,index in first_run['stops'].items():
    info_stop='define hidden fastcc void @tbimp_'+name+'(ptr %cpu) { call void @qol_missing_import(ptr %cpu, i32 '+str(index)+')\n unreachable\n}'
    assert stops_text.count(info_stop)==1,name
    stops_text=stops_text.replace(info_stop,'',1)
wave_stops.write_text(stops_text);sources.append(info_wrapper)
sources[sources.index(old_stops)]=wave_stops;sources.append(wave_wrapper)
# Refresh finite host exports without relifting unchanged original images.
from graphics_backend_inputs import prepare as graphics_inputs
graphics_sources,_=graphics_inputs(mobile_inputs)
old_wrapper=next(p for p in sources if p.name=='graphics-bindings.ll')
sources[sources.index(old_wrapper)]=next(p for p in graphics_sources if p.name=='graphics-bindings.ll')
sdk_name='iphoneos' if a.platform=='device' else 'iphonesimulator'
target='arm64-apple-ios16'+('' if a.platform=='device' else '-simulator');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
sdl,sdl_flags,libraries=sdl_inputs(a.platform);gl4es,gl_flags,gl_libraries=gl4es_inputs(a.platform);math=sse_inputs(a.platform)
angle=None
if a.angle:
 from angle_backend_inputs import inputs as angle_inputs,bundle as angle_bundle
 angle,angle_flags,angle_libraries=angle_inputs(a.platform)
 gl_flags+=angle_flags;gl_libraries=angle_libraries+gl_libraries

http_root=R/'generated/http-backend'/a.platform;http=json.loads((http_root/'result.json').read_text());assert http['passed'] and http['platform']==a.platform
for name,h in http['libraries'].items():assert sha(R/name)==h,name
al=json.loads((R/'generated/openal-soft'/a.platform/'result.json').read_text());assert al['platform']==a.platform and sha(R/al['library'])==al['library_sha256']
zbuild=R/'generated/zlib-ng'/a.platform/'build'
zmeta=json.loads((zbuild.parent/'result.json').read_text());assert zmeta['pass'] and zmeta['platform']==a.platform and sha(zbuild/'libz-ng.a')==zmeta['library_sha256']
flags=(['-DTRIBES_HARDWARE_PERMISSIONS'] if a.hardware_permissions else [])+['-std=c11','-DPTROFS_64BIT','-DTB_CHECKED_IMAGE','-DTB_GUARD_EXTERNALS','-DTB_CHECKED_SUBPAGES','-DTB_CAPTURE_PATCHES','-DTB_PUBLISH_PATCHES','-DTB_MOBILE_ENGINE','-DTB_MOBILE_LEGACY_FONT_BANK','-I',str(mobile_inputs),'-I',str(source),'-I',str(zbuild),'-I',str(R/'ref/zlib-ng'),'-I',str(http_root/'install/include'),'-DAL_LIBTYPE_STATIC','-I',str(R/'ref/openal-soft/include'),*sdl_flags,*gl_flags,*math['compile_flags']]
# Replace a prior successful receipt before long code generation starts. A
# retained app is not evidence that this invocation has finished building it.
(out/'result.json').write_text(json.dumps(dict(state='building',platform=a.platform,pid=os.getpid()),indent=2)+'\n')
objects,_=compile_support(sources,out/'support',flags,target=target,sdk_name=sdk_name,defer_codegen=True,cpu_host=a.cpu_host_checks,hardware=a.hardware_permissions)
meta=json.loads((out/'support/result.json').read_text());entries=[]
for name in objects:
 p=pathlib.Path(name);row=meta['objects'][p.stem]
 entries.append(dict(object=name,object_sha256=sha(p),bitcode=None if row['trusted_barrier_runtime'] else str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,_=guard_link(entries+bochs,out/'guarded',target=target)
profile=out/'game-profile'
if not profile.exists():shutil.copytree(R/'generated/apple-game-profile',profile,symlinks=True)
# Use the game's supported offline launch override in this isolated profile.
# No authentication or online match joining during mobile development.
offline=profile/'base/autoexec.cs'
offline_text='// TribesPad mobile development: original offline mode.\n$SkipLogin = true;\n$LaunchMode = "Offline";\n'
if offline.exists():assert offline.read_text()==offline_text, 'Preserve custom autoexec.cs; select offline mode there before building'
else:offline.write_text(offline_text)

volume=out/'volume';temp=volume/'TribesPad/Temp';temp.mkdir(parents=True,exist_ok=True)
# Publication descriptions and loaded original images are immutable inputs;
# capture output goes to this app's separate directory.
capture=out/'capture';capture.mkdir(exist_ok=True);shutil.copy2(source/'published-code.bin',capture/'published-code.bin')
# The original bootstrap loads Miles and its providers from capture_directory.
# Keep these immutable inputs beside publication data, separate from captures.
from miles_runtime_inputs import SOURCE as miles_source
image_sources={'miles.bin':miles_source}
for row in json.loads((R/'config/runtime/miles-codecs.json').read_text()):
 image_sources[row['module']+'.bin']=R/row['lift']/'native-diagnostic'
for name,origin in image_sources.items():
 expected=json.loads((origin/'result.json').read_text())['mapped_sha256']
 assert sha(source/name)==expected,name
 shutil.copy2(source/name,capture/name)
app=out/'TribesPad.app';app.mkdir(exist_ok=True);exe=app/'TribesPad'
if not a.angle:
 # Remove only this backend's owned files when switching back to EAGL.
 for name in ('libEGL.framework','libGLESv2.framework'):
  framework=app/'Frameworks'/name
  if framework.exists():shutil.rmtree(framework)
 license=app/'LICENSE.ANGLE'
 if license.exists():license.unlink()
shutil.copy2(R/'generated/gl4es'/a.platform/'LICENSE.gl4es',app/'LICENSE.gl4es')
args=[str(exe),str(source/'engine.bin'),str(source/'qol.bin'),str(volume),str(profile),str(temp),str(capture),'engine']
if bundled:
 data=app/'GameData';data.mkdir(exist_ok=True)
 # Incremental rsync avoids recopying hundreds of MB on native-code-only builds.
 # The isolated source profile is copied without following external symlinks.
 assert not any(p.is_symlink() for p in profile.rglob('*')), 'Bundled profile must not contain symlinks'
 subprocess.run(['rsync','-a','--delete',str(profile)+'/',str(data/'Profile')+'/'],check=True)
 loader=data/'Loader';loader.mkdir(exist_ok=True)
 for name in ['published-code.bin',*image_sources]:shutil.copy2(capture/name,loader/name)
 for name in ['engine.bin','qol.bin']:shutil.copy2(source/name,data/name)
 config='#define TB_BUNDLED_GAME_DATA 1\n'
else:
 config='#define TB_BUNDLED_GAME_DATA 0\nstatic const char *launch_workdir='+json.dumps(str(R))+';\nstatic char *launch_arguments[]={'+','.join(json.dumps(p) for p in args)+',NULL};\n'
(out/'mobile-engine-launch-config.h').write_text(config)
host=[R/'port/apple/mobile_engine_main.m',R/'port/apple/mobile_game_paths.m',R/'port/apple/sdl_main.c',R/'port/apple/gl4es_context.c',R/'port/apple/mobile_controls.m',R/'port/apple/mobile_controls.c',R/'port/core/input.c']
if a.angle:host.append(R/'port/apple/angle_sdl.c')
command=['clang','-target',target,'-isysroot',sdk,'-O2','-I',str(out),*sdl_flags,*gl_flags,'-DNOX11','-DNOEGL','-DNO_LOADER','-DDEFAULT_ES=2','-isystem',str(R/'ref/gl4es/src'),*map(str,host),*objects,*gl_libraries,*libraries,*math['libraries'],*[str(R/n) for n in http['libraries']],str(R/al['library']),str(zbuild/'libz-ng.a'),'-lz','-lc++','-framework','Foundation','-framework','GameController','-framework','CoreGraphics','-framework','CoreText','-framework','CoreFoundation','-framework','SystemConfiguration','-framework','AudioToolbox','-framework','CoreAudio','-o',str(exe)]
command[1:1]=['-fobjc-arc']
with (out/'link.log').open('w') as log:subprocess.run(command,stdout=log,stderr=log,check=True)
identifier='com.tribespad.full-engine.ipad'
from app_icon import compile_icon
icon_keys=compile_icon(app,a.platform)
(app/'Info.plist').write_bytes(plistlib.dumps(dict(**icon_keys,CFBundleDisplayName='TribesPad',CFBundleIdentifier=identifier,CFBundleExecutable='TribesPad',CFBundleName='TribesPad',CFBundlePackageType='APPL',CFBundleSupportedPlatforms=['iPhoneOS' if a.platform=='device' else 'iPhoneSimulator'],CFBundleVersion='1',CFBundleShortVersionString='0.1',MinimumOSVersion='16.0',UIDeviceFamily=[1,2],UIRequiresFullScreen=True,LSRequiresIPhoneOS=True,UIRequiredDeviceCapabilities=['arm64'],NSLocalNetworkUsageDescription='Find and connect to local Tribes 2 games.',UIApplicationSupportsIndirectInputEvents=True,UILaunchScreen={},UISupportedInterfaceOrientations=['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'])))
signing=['--entitlements',str(R/'port/apple/TribesPad.entitlements')] if a.platform=='device' else []
if a.angle:
 for framework in angle_bundle(app,a.platform):subprocess.run(['codesign','--force','--sign','-',str(framework)],check=True)
subprocess.run(['codesign','--force','--sign','-',*signing,str(app)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
(out/'result.json').write_text(json.dumps(dict(state='built',platform=a.platform,cpu_host_checks=a.cpu_host_checks,hardware_permissions=a.hardware_permissions,bundled_assets=bundled,entitlements_sha256=sha(R/'port/apple/TribesPad.entitlements') if a.platform=='device' else None,binary_sha256=sha(exe),prepared_sha256=sha(source/'prepared-inputs.json'),support_sha256=sha(out/'support/result.json'),guarded_sha256=sha(out/'guarded/result.json'),gl4es=gl4es,angle=angle,openal=al,arguments=None if bundled else args,mobile_hooks_sha256=sha(mobile_inputs/'mobile-hooks.json'),x87_stores_sha256=sha(mobile_inputs/'x87-stores.json'),mp3_stores_sha256=sha(mobile_inputs/'mp3-stores.json'),wav_io_test_sha256=sha(wave_proof_path),mobile_gpu_timing=dict(available=False,original_blocks=timing_blocks,test_sha256=sha(timing_proof),translated_sha256=sha(mobile_qol)),scope='Full checked engine linked for '+a.platform+'. Device builds need development signing. Build is not rendering or gameplay acceptance.'),indent=2)+'\n')
if not a.build_only:
 subprocess.run(['xcrun','simctl','install',a.simulator_device,str(app)],check=True)
 for name in ('stdout.log','stderr.log'):(out/name).write_text('')
 subprocess.run(['xcrun','simctl','launch','--stdout='+str(out/'stdout.log'),'--stderr='+str(out/'stderr.log'),a.simulator_device,identifier],check=True)
print(app)
