#!/usr/bin/env python3
"""Exercise native Cocoa/UIKit window and GL context ownership through guest ABI in an Apple app."""
import argparse,hashlib,importlib.util,json,pathlib,plistlib,re,subprocess,time
from checked_support import compile_support
from guard_checked_link import guard_link
from sdl_backend_inputs import inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');ap.add_argument('--graphics',action='store_true');a=ap.parse_args();platform='simulator' if a.simulator_device else 'macos'
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [x['udid'] for row in d['devices'].values() for x in row if x['state']=='Booted']==[a.simulator_device]
out=R/'generated/integration-review'/('gl-query-init' if a.graphics else 'sdl-window')/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"passed":false,"state":"building"}\n')
fixture=R/'generated/integration-review/cursor-surface/execution/cursor-rgba.bin'
assert sha(fixture)=='130a25b7967d4e73a113cc55f0c273dad4caba60eef07fd505393ab2830eb1d7'
(out/'cursor-fixture.h').write_text('static const unsigned char cursor_fixture[]={'+','.join(str(x) for x in fixture.read_bytes())+'};\n')
backend,flags,libraries=inputs(platform);target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12'
from sse_build_inputs import sse_inputs
math=sse_inputs(platform);flags+=['-I',str(out)]+math['compile_flags'];libraries+=math['libraries']
bochs=R/'generated/bochs-math';bochs=bochs/'simulator' if a.simulator_device else bochs
bm=json.loads((bochs/'result.json').read_text());assert sha(R/'port/bochs-support/math_bridge.cc')==bm['bridge_sha256'] and sha(bochs/'math_bridge.o')==bm['native_bridge_object_sha256'] and sha(bochs/'libbochs-math.a')==bm['library_sha256']
libraries += [str(bochs/'math_bridge.o'),str(bochs/'libbochs-math.a'),'-lc++']
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=json.loads((R/'config/runtime/sdl-bindings.json').read_text())
for name,helper,n,returns in [('TestMalloc','sdl_test_malloc',1,True),('TestCalloc','sdl_test_calloc',2,True),('TestRealloc','sdl_test_realloc',2,True),('TestFree','sdl_test_free',1,False),('TestLog','sdl_test_log',4,False),('TestGuestFree','tribes_sdl_free',1,False)]:bindings[name]=dict(helper=helper,arguments=n,returns=returns,guest_base=False,cpu_argument=True,calling_convention='cdecl')
# Independent original-source fixtures; the production template generator is not used here.
if a.graphics:
 import pefile,io,zipfile
 image=R/'ref/qol/IFC22.dll';assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 pe=pefile.PE(str(image));archive=zipfile.ZipFile(io.BytesIO(pe.get_data(4677872,21234)))
 variants=[('gui',''),('gui','#define PREMULTIPLY_ALPHA 1\n'),('blender',''),('post',''),('post','#define GUI 1\n'),('post','#define DEPTH 1\n#define GAMMA 1\n'),('post','#define DEPTH 2\n#define GUI 1\n#define GAMMA 1\n'),('terrain',''),('terrain','#define DEPTH_ONLY 1\n'),('text',''),('textMSDF','')]
 shader_rows=[]
 for stem,defines in variants:
  sources=[archive.read(stem+'.'+ext).decode().replace('#inject','#line       1        1 \n'+defines+'\n#line      4        0 ') for ext in ['vert','frag']]
  shader_rows.append(' {'+','.join(map(json.dumps,[stem]+sources))+'},\n')
 (out/'original-shader-fixture.h').write_text('static const struct {const char *name,*vertex,*fragment;} original_shaders[]={\n'+''.join(shader_rows)+'};\n')
graphics_sources=[]
if a.graphics:
 from graphics_backend_inputs import prepare as prepare_graphics
 graphics_sources,graphics_bindings=prepare_graphics(out)
 flags+=['-DGRAPHICS','-I',str(out)]
wrapper=out/'bindings.ll';wrapper.write_text(gen.generate(bindings,False))
sources=[R/n for n in ['port/core/guest_sdl_backend.c','port/core/guest_window_catalog.c','port/core/ext80.c','port/llasm-support/tribes_float80.c','port/llasm-support/tribes_result.c','port/core/guest_sdl_allocator.c','port/core/guest_sdl_sync.c','port/core/guest_sdl_tls.c','port/core/guest_sdl_memory.c','port/core/guest_curl_allocator.c','port/core/guest_callback.c','port/core/guest_heap.c','port/core/process_environment.c','port/core/guest_filesystem.c','tests/fixtures/sdl_window_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
if a.graphics:sources+=graphics_sources+[R/'port/core/guest_modules.c']
wrong=out/'wrong-signatures.ll';wrong_text='declare ptr @SDL_CreateWindow(ptr,i32,i32,i32)\ndeclare ptr @SDL_GL_CreateContext(i32)\ndefine void @bad_window(){ %r = call ptr @SDL_CreateWindow(ptr null,i32 1,i32 1,i32 2)\n ret void\n}\ndefine void @bad_context(){ %r = call ptr @SDL_GL_CreateContext(i32 1)\n ret void\n}\n'
if a.graphics and not a.simulator_device:wrong_text+='declare void @glFrustum(i32,i32,i32,i32,i32,i32)\ndefine void @bad_frustum(){ call void @glFrustum(i32 0,i32 0,i32 0,i32 0,i32 0,i32 0)\n ret void\n}\ndeclare void @glMultMatrixf(i32)\ndefine void @bad_mult_matrix(){ call void @glMultMatrixf(i32 0)\n ret void\n}\n'
if a.graphics and not a.simulator_device:wrong_text+='declare void @glDepthMask(i32)\ndefine void @bad_depth_mask(){ call void @glDepthMask(i32 1)\n ret void\n}\ndeclare i32 @glIsEnabled(i32)\ndefine void @bad_is_enabled(){ %r = call i32 @glIsEnabled(i32 0)\n ret void\n}\n'
if a.graphics and not a.simulator_device:wrong_text+='declare void @glScalef(i32,i32,i32)\ndefine void @bad_scale(){ call void @glScalef(i32 1,i32 1,i32 1)\n ret void\n}\n'
if a.graphics and not a.simulator_device:wrong_text+='declare void @glMultMatrixd(i32)\ndefine void @bad_mult_matrix_double(){ call void @glMultMatrixd(i32 0)\n ret void\n}\n'
if a.graphics and not a.simulator_device:wrong_text+='declare void @glPolygonOffset(i32,i32)\ndefine void @bad_polygon_offset(){ call void @glPolygonOffset(i32 0,i32 0)\n ret void\n}\n'
if a.graphics and not a.simulator_device:wrong_text+='declare void @glColor4f(i32,i32,i32,i32)\ndefine void @bad_color4f(){ call void @glColor4f(i32 0,i32 0,i32 0,i32 0)\n ret void\n}\ndeclare void @glVertex2i(float,float)\ndefine void @bad_vertex2i(){ call void @glVertex2i(float 0.0,float 0.0)\n ret void\n}\n'
wrong.write_text(wrong_text)
objects,checked=compile_support(sources+[wrong],out/'checked',['-DPTROFS_64BIT',*flags],target=target,sdk_name='iphonesimulator' if a.simulator_device else 'macosx');meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
host=[R/'tests/sdl_window_test.c',R/'port/apple/sdl_main.c'];guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];runs=[]
def execute(app,exe,identifier,label,argument=None):
 if not a.simulator_device:
  run=subprocess.run([str(exe)]+([argument] if argument else []),capture_output=True,text=True,timeout=60)
  (out/(label+'-stdout.log')).write_text(run.stdout);(out/(label+'-stderr.log')).write_text(run.stderr)
  return run.returncode,run.stdout,run.stderr
 command=['xcrun','simctl'];stdout=out/(label+'-stdout.log');stderr=out/(label+'-stderr.log');stdout.write_text('');stderr.write_text('')
 subprocess.run(command+['terminate',a.simulator_device,identifier],capture_output=True)
 launch=subprocess.run(command+['launch','--stdout='+str(stdout),'--stderr='+str(stderr),a.simulator_device,identifier]+([argument] if argument else []),capture_output=True,text=True,timeout=30);assert not launch.returncode,(launch.stdout,launch.stderr)
 (out/(label+'-launch.log')).write_text(launch.stdout+launch.stderr);pid=int(re.search(r': (\d+)\s*$',launch.stdout).group(1))
 def live():return 'SDLWindowProbe' in subprocess.run(['ps','-p',str(pid),'-o','comm='],capture_output=True,text=True).stdout
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
 app=out/(mode+'.app');exe=app/('SDLWindowProbe' if a.simulator_device else 'Contents/MacOS/SDLWindowProbe');exe.parent.mkdir(parents=True,exist_ok=True)
 identifier='com.tribespad.sdl-window-probe.'+mode;info=dict(CFBundleIdentifier=identifier,CFBundleExecutable='SDLWindowProbe',CFBundleName='TribesPad SDL Window Probe',CFBundlePackageType='APPL',CFBundleVersion='1',CFBundleShortVersionString='1.0')
 info.update(dict(MinimumOSVersion='16.0',UIDeviceFamily=[1,2],UIRequiresFullScreen=True,UIApplicationSupportsIndirectInputEvents=True,UILaunchScreen={},UISupportedInterfaceOrientations=['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'])) if a.simulator_device else info.update(dict(LSMinimumSystemVersion='12.0',NSHighResolutionCapable=True))
 plist=app/('Info.plist' if a.simulator_device else 'Contents/Info.plist');plist.write_bytes(plistlib.dumps(info))
 cmd=['clang','-target',target,'-isysroot',backend['sdk'],'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*flags,*map(str,host+guards)]
 cmd+=['-DCHECKED',*objects] if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+libraries+['-o',str(exe)],check=True)
 subprocess.run(['codesign','--force','--sign','-',str(app)],check=True,capture_output=True)
 if a.simulator_device:subprocess.run(['xcrun','simctl','install',a.simulator_device,str(app)],check=True)
 code,text,error=execute(app,exe,identifier,mode);assert code==0 and 'PASS:' in text and 'TribesPad:' not in error and 'ThreadSanitizer:' not in error,(mode,code,text,error)
 faults=[]
 if mode=='checked':
  for case in ['readonly-event','cross-page-event','mutated-event-text','unsupported-event','unreadable-cursor','readonly-cursor','mutated-surface','stale-surface','mutated-remove-surface','stale-remove-surface','stale-cursor','readonly-closest','mutated-closest','mutated-mode','unknown-mode','unknown-property','stale-properties','readonly-count','wrong-thread','readonly-output','wrong-token','stale-window','stale-context','live-subsystem-quit','reentrant-window','bad-window','bad-context']+(['readonly-buffers','overflow-buffers','unreadable-buffer-data','overflow-buffer-data','unreadable-buffer-names','readonly-textures','unreadable-texture','overflow-texture','wrong-dc','readonly-gl','readonly-gl-float','unsupported-gl-float','mutated-string'] if a.graphics else [])+(['sync-output','sync-length','array-pixels','array-sub-pixels','array-overflow','array-pbo','uniform-vector','uniform-overflow','shader-pointers','shader-lengths','shader-string','shader-status','shader-active','shader-log','quad-indices','quad-vertices','quad-texcoord-range','quad-color-unreadable','quad-color-range','quad-vbo-pointer','quad-element-buffer','unreadable-matrix','shared-array-delete','shared-copy-delete','readonly-query-result','cross-page-query-result','unreadable-gl-vector','readonly-queries','overflow-queries','mipmap-nonzero-base','bad-frustum','bad-mult-matrix','bad-depth-mask','bad-is-enabled','bad-scale','bad-polygon-offset','bad-color4f','bad-vertex2i','matrix-mult-unreadable','matrix-mult-overflow','matrix-double-unreadable','matrix-double-overflow','bad-mult-matrix-double','state-color','state-material','state-texenv','state-fog-enable','normal-unreadable','normal-range','normal-overflow','normal-vbo'] if a.graphics and not a.simulator_device else []):
   code,_,error=execute(app,exe,identifier,mode+'-'+case,case)
   expected='SDL video requires Apple main application entry' if case=='wrong-thread' else 'checked guest memory violation' if case in ['sync-output','sync-length','array-pixels','array-sub-pixels','uniform-vector','shader-pointers','shader-lengths','shader-string','shader-status','shader-active','shader-log','quad-indices','quad-vertices','quad-texcoord-range','quad-color-unreadable','quad-color-range','unreadable-matrix','matrix-mult-unreadable','matrix-double-unreadable','state-color','state-material','state-texenv','normal-unreadable','normal-range','readonly-query-result','cross-page-query-result','readonly-event','cross-page-event','unreadable-cursor','readonly-cursor','readonly-buffers','unreadable-buffer-data','unreadable-buffer-names','readonly-textures','unreadable-texture','readonly-output','readonly-gl','readonly-count','readonly-closest','unreadable-gl-vector','readonly-gl-float','readonly-queries'] else 'external memory contract stop: unverified call' if case.startswith('bad-') else 'SDL video subsystem quit with live graphics' if case=='live-subsystem-quit' else 'unsupported SDL backend ownership/lifecycle'
   assert code==-6 and expected in error,(case,code,error);faults.append(case)
 runs.append(dict(mode=mode,passed=True,stdout=text,app=str(app.relative_to(R)),binary_sha256=sha(exe),faults=faults));print(mode,text.strip(),flush=True)
paths=sources+host+guards+[wrong,fixture,out/'cursor-fixture.h']+([out/'graphics-exports.inc',out/'original-shader-fixture.h',R/'port/core/legacy_shader_sources.inc',R/'scripts/generate-legacy-shaders.py',R/'scripts/graphics_backend_inputs.py',R/'port/core/guest_graphics.h'] if a.graphics else [])+[R/'port/apple/sdl_main.h',R/'port/core/guest_sdl_backend.h',R/'config/runtime/sdl-bindings.json',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',R/'scripts/sdl_backend_inputs.py',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',simulator_device=a.simulator_device,runs=runs,math_evidence=math['evidence'],bochs_report_sha256=sha(bochs/'result.json'),backend_sha256=sha(R/'generated/sdl-backend'/platform/'result.json'),checked_sha256=checked,guarded_sha256=guarded,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope=('Finite guest GL/GLU export loading, scalar/string query dispatch and window/DC extension queries; ' if a.graphics else '')+'Native SDL window/context lifecycle through emitted guest ABI, real drawable pixel readback and typed handles. GL scalar/string queries run through guest export dispatch when graphics mode is enabled; clear/framebuffer drawing runs through guest exports and readback uses a native-host oracle. No original game window or gameplay acceptance.'),indent=2)+'\n')
