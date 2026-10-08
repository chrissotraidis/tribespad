#!/usr/bin/env python3
"""Exercise native Cocoa/UIKit VIDEO startup through guest ABI in an Apple app."""
import argparse,hashlib,importlib.util,json,pathlib,plistlib,re,subprocess,time
from checked_support import compile_support
from guard_checked_link import guard_link
from sdl_backend_inputs import inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();platform='simulator' if a.simulator_device else 'macos'
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [x['udid'] for row in d['devices'].values() for x in row if x['state']=='Booted']==[a.simulator_device]
out=R/'generated/integration-review/sdl-audio'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"passed":false,"state":"building"}\n')
backend,flags,libraries=inputs(platform);flags+=['-DTRIBESPAD_SDL_TEST_OBSERVER'];target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12'
from sse_build_inputs import sse_inputs
math=sse_inputs(platform);flags+=math['compile_flags'];libraries+=math['libraries']
bochs=R/'generated/bochs-math';bochs=bochs/'simulator' if a.simulator_device else bochs
bm=json.loads((bochs/'result.json').read_text());assert sha(R/'port/bochs-support/math_bridge.cc')==bm['bridge_sha256'] and sha(bochs/'math_bridge.o')==bm['native_bridge_object_sha256'] and sha(bochs/'libbochs-math.a')==bm['library_sha256']
libraries += [str(bochs/'math_bridge.o'),str(bochs/'libbochs-math.a'),'-lc++']
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=json.loads((R/'config/runtime/sdl-bindings.json').read_text())
for name,helper,n,returns in [('TestMalloc','sdl_test_malloc',1,True),('TestCalloc','sdl_test_calloc',2,True),('TestRealloc','sdl_test_realloc',2,True),('TestFree','sdl_test_free',1,False),('TestLog','sdl_test_log',4,False)]:bindings[name]=dict(helper=helper,arguments=n,returns=returns,guest_base=False,cpu_argument=True,calling_convention='cdecl')
wrapper=out/'bindings.ll';wrapper.write_text(gen.generate(bindings,False))
sources=[R/n for n in ['port/core/guest_sdl_backend.c','port/core/guest_window_catalog.c','port/core/ext80.c','port/llasm-support/tribes_float80.c','port/llasm-support/tribes_result.c','port/core/guest_sdl_sync.c','port/core/guest_sdl_tls.c','port/core/guest_sdl_memory.c','port/core/guest_curl_allocator.c','port/core/guest_callback.c','port/core/guest_heap.c','port/core/process_environment.c','port/core/guest_filesystem.c','tests/fixtures/sdl_hint_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
objects,checked=compile_support(sources,out/'checked',['-DPTROFS_64BIT',*flags],target=target,sdk_name='iphonesimulator' if a.simulator_device else 'macosx');meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
host=[R/'tests/sdl_audio_test.c',R/'port/apple/sdl_main.c'];guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];runs=[]
def execute(app,exe,identifier,label,argument=None):
 if not a.simulator_device:
  run=subprocess.run([str(exe)]+([argument] if argument else []),capture_output=True,text=True,timeout=60)
  (out/(label+'-stdout.log')).write_text(run.stdout);(out/(label+'-stderr.log')).write_text(run.stderr)
  return run.returncode,run.stdout,run.stderr
 command=['xcrun','simctl'];stdout=out/(label+'-stdout.log');stderr=out/(label+'-stderr.log');stdout.write_text('');stderr.write_text('')
 subprocess.run(command+['terminate',a.simulator_device,identifier],capture_output=True)
 launch=subprocess.run(command+['launch','--stdout='+str(stdout),'--stderr='+str(stderr),a.simulator_device,identifier]+([argument] if argument else []),capture_output=True,text=True,timeout=30);assert not launch.returncode,(launch.stdout,launch.stderr)
 (out/(label+'-launch.log')).write_text(launch.stdout+launch.stderr);pid=int(re.search(r': (\d+)\s*$',launch.stdout).group(1))
 def live():return 'SDLAudioProbe' in subprocess.run(['ps','-p',str(pid),'-o','comm='],capture_output=True,text=True).stdout
 try:
  end=time.monotonic()+30
  while time.monotonic()<end:
   text=stdout.read_text();error=stderr.read_text()
   if argument:
    if 'TribesPad:' in error and not live():return -6,text,error
   elif 'PASS:' in text:
    time.sleep(1);assert live(),('post-main app exited unexpectedly',text,stderr.read_text());return 0,text,stderr.read_text()
   elif not live():raise AssertionError(('application exited',text,error))
   time.sleep(.2)
  raise AssertionError(('application timeout',stdout.read_text(),stderr.read_text()))
 finally:subprocess.run(command+['terminate',a.simulator_device,identifier],capture_output=True)
for mode in (['checked'] if a.simulator_device else ['normal','tsan','checked']):
 app=out/(mode+'.app');exe=app/('SDLAudioProbe' if a.simulator_device else 'Contents/MacOS/SDLAudioProbe');exe.parent.mkdir(parents=True,exist_ok=True)
 identifier='com.tribespad.sdl-audio-probe.'+mode;info=dict(CFBundleIdentifier=identifier,CFBundleExecutable='SDLAudioProbe',CFBundleName='TribesPad SDL Audio Probe',CFBundlePackageType='APPL',CFBundleVersion='1',CFBundleShortVersionString='1.0')
 info.update(dict(MinimumOSVersion='16.0',UIDeviceFamily=[1,2],UIRequiresFullScreen=True,UIApplicationSupportsIndirectInputEvents=True,UILaunchScreen={},UISupportedInterfaceOrientations=['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'])) if a.simulator_device else info.update(dict(LSMinimumSystemVersion='12.0',NSHighResolutionCapable=True))
 plist=app/('Info.plist' if a.simulator_device else 'Contents/Info.plist');plist.write_bytes(plistlib.dumps(info))
 cmd=['clang','-target',target,'-isysroot',backend['sdk'],'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*flags,*map(str,host+guards)]
 cmd+=['-DCHECKED',*objects] if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+libraries+['-o',str(exe)],check=True)
 subprocess.run(['codesign','--force','--sign','-',str(app)],check=True,capture_output=True)
 if a.simulator_device:subprocess.run(['xcrun','simctl','install',a.simulator_device,str(app)],check=True)
 code,text,error=execute(app,exe,identifier,mode);assert code==0 and text.startswith('PASS:') and 'TribesPad:' not in error and 'ThreadSanitizer:' not in error,(mode,code,text,error)
 faults=[]
 if mode=='checked':
  for case in ['no-host-entry','wrong-thread','unclean-return','after-finish','foreign-stream','stale-stream','stale-device','unreadable-spec','readonly-format','readonly-data','overflow-data','after-audio-quit','unreadable-pcm','overflow-pcm']:
   code,_,error=execute(app,exe,identifier,mode+'-'+case,case);assert code==-6 and ('SDL video requires Apple main application entry' in error if case in ['no-host-entry','wrong-thread'] else 'TribesPad:' in error),(case,code,error);faults.append(case)
 runs.append(dict(mode=mode,passed=True,stdout=text,app=str(app.relative_to(R)),binary_sha256=sha(exe),faults=faults));print(mode,text.strip(),flush=True)
paths=sources+host+guards+[R/'port/apple/sdl_main.h',R/'port/core/guest_sdl_backend.h',R/'config/runtime/sdl-bindings.json',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',R/'scripts/sdl_backend_inputs.py',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',simulator_device=a.simulator_device,runs=runs,math_evidence=math['evidence'],bochs_report_sha256=sha(bochs/'result.json'),backend_sha256=sha(R/'generated/sdl-backend'/platform/'result.json'),checked_sha256=checked,guarded_sha256=guarded,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Real Cocoa/UIKit video and guest CoreAudio device/stream setup, native PCM conversion with exact checked output extent, playback/pause, ownership and shutdown plus copied native worker logs through checked guest ABI and private guest allocator under Apple application main. Native bootstrap and test harness are host-owned, not guest code. No original engine window, event conversion, renderer, controls or multiplayer acceptance.'),indent=2)+'\n')
