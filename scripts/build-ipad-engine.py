#!/usr/bin/env python3
"""Build and launch the full checked original engine in the sole iPad Simulator.

Run test-qol-native-bootstrap.py --enter-engine --probe-ntdll --prepare-only first.
This development app uses prepared workspace assets; it is not a device IPA.
"""
import argparse,hashlib,json,pathlib,plistlib,shutil,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
from checked_bochs_inputs import checked_bochs_inputs
from sdl_backend_inputs import inputs as sdl_inputs
from gl4es_backend_inputs import inputs as gl4es_inputs
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--simulator-device',required=True)
ap.add_argument('--prepared-directory',type=pathlib.Path,default=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe')
ap.add_argument('--build-only',action='store_true')
a=ap.parse_args();source=a.prepared_directory.resolve();out=R/'generated/ipad-engine';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
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
# Refresh finite host exports without relifting unchanged original images.
from graphics_backend_inputs import prepare as graphics_inputs
graphics_sources,_=graphics_inputs(mobile_inputs)
old_wrapper=next(p for p in sources if p.name=='graphics-bindings.ll')
sources[sources.index(old_wrapper)]=next(p for p in graphics_sources if p.name=='graphics-bindings.ll')
target='arm64-apple-ios16-simulator';sdk=subprocess.check_output(['xcrun','--sdk','iphonesimulator','--show-sdk-path'],text=True).strip()
sdl,sdl_flags,libraries=sdl_inputs('simulator');gl4es,gl_flags,gl_libraries=gl4es_inputs('simulator');math=sse_inputs('simulator')
http_root=R/'generated/http-backend/simulator';http=json.loads((http_root/'result.json').read_text());assert http['passed'] and http['platform']=='simulator'
for name,h in http['libraries'].items():assert sha(R/name)==h,name
al=json.loads((R/'generated/openal-soft/simulator/result.json').read_text());assert al['platform']=='simulator' and sha(R/al['library'])==al['library_sha256']
zbuild=R/'generated/zlib-ng/simulator/build'
zmeta=json.loads((zbuild.parent/'result.json').read_text());assert zmeta['pass'] and zmeta['platform']=='simulator' and sha(zbuild/'libz-ng.a')==zmeta['library_sha256']
flags=['-std=c11','-DPTROFS_64BIT','-DTB_CHECKED_IMAGE','-DTB_GUARD_EXTERNALS','-DTB_CHECKED_SUBPAGES','-DTB_CAPTURE_PATCHES','-DTB_PUBLISH_PATCHES','-DTB_MOBILE_ENGINE','-DTB_MOBILE_LEGACY_FONT_BANK','-I',str(mobile_inputs),'-I',str(source),'-I',str(zbuild),'-I',str(R/'ref/zlib-ng'),'-I',str(http_root/'install/include'),'-DAL_LIBTYPE_STATIC','-I',str(R/'ref/openal-soft/include'),*sdl_flags,*gl_flags,*math['compile_flags']]
objects,_=compile_support(sources,out/'support',flags,target=target,sdk_name='iphonesimulator')
meta=json.loads((out/'support/result.json').read_text());entries=[]
for name in objects:
 p=pathlib.Path(name);row=meta['objects'][p.stem]
 entries.append(dict(object=name,object_sha256=sha(p),bitcode=None if row['trusted_barrier_runtime'] else str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
bochs,_=checked_bochs_inputs('simulator');objects,_=guard_link(entries+bochs,out/'guarded',target=target)
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
shutil.copy2(R/'generated/gl4es/simulator/LICENSE.gl4es',app/'LICENSE.gl4es')
args=[str(exe),str(source/'engine.bin'),str(source/'qol.bin'),str(volume),str(profile),str(temp),str(capture),'engine']
(out/'mobile-engine-launch-config.h').write_text('static const char *launch_workdir='+json.dumps(str(R))+';\nstatic char *launch_arguments[]={'+','.join(json.dumps(p) for p in args)+',NULL};\n')
host=[R/'port/apple/mobile_engine_main.c',R/'port/apple/sdl_main.c',R/'port/apple/gl4es_context.c',R/'port/apple/mobile_controls.m',R/'port/apple/mobile_controls.c',R/'port/core/input.c']
command=['clang','-target',target,'-isysroot',sdk,'-O2','-I',str(out),*sdl_flags,*gl_flags,'-DNOX11','-DNOEGL','-DNO_LOADER','-DDEFAULT_ES=2','-isystem',str(R/'ref/gl4es/src'),*map(str,host),*objects,*gl_libraries,*libraries,*math['libraries'],*[str(R/n) for n in http['libraries']],str(R/al['library']),str(zbuild/'libz-ng.a'),'-lz','-lc++','-framework','Foundation','-framework','CoreGraphics','-framework','CoreText','-framework','CoreFoundation','-framework','SystemConfiguration','-framework','AudioToolbox','-framework','CoreAudio','-o',str(exe)]
command[1:1]=['-fobjc-arc']
with (out/'link.log').open('w') as log:subprocess.run(command,stdout=log,stderr=log,check=True)
identifier='com.tribespad.full-engine.ipad'
(app/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier=identifier,CFBundleExecutable='TribesPad',CFBundleName='TribesPad',CFBundlePackageType='APPL',CFBundleVersion='1',CFBundleShortVersionString='0.1',MinimumOSVersion='16.0',UIDeviceFamily=[2],UIRequiresFullScreen=True,UIApplicationSupportsIndirectInputEvents=True,UILaunchScreen={},UISupportedInterfaceOrientations=['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'])))
subprocess.run(['codesign','--force','--sign','-',str(app)],check=True)
(out/'result.json').write_text(json.dumps(dict(state='built',binary_sha256=sha(exe),prepared_sha256=sha(source/'prepared-inputs.json'),support_sha256=sha(out/'support/result.json'),guarded_sha256=sha(out/'guarded/result.json'),gl4es=gl4es,openal=al,arguments=args,mobile_hooks_sha256=sha(mobile_inputs/'mobile-hooks.json'),mobile_gpu_timing=dict(available=False,original_blocks=timing_blocks,test_sha256=sha(timing_proof),translated_sha256=sha(mobile_qol)),scope='Full checked engine linked for iPad Simulator. Build is not rendering or gameplay acceptance.'),indent=2)+'\n')
if not a.build_only:
 subprocess.run(['xcrun','simctl','install',a.simulator_device,str(app)],check=True)
 for name in ('stdout.log','stderr.log'):(out/name).write_text('')
 subprocess.run(['xcrun','simctl','launch','--stdout='+str(out/'stdout.log'),'--stderr='+str(out/'stderr.log'),a.simulator_device,identifier],check=True)
print(app)
