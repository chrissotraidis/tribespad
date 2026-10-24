#!/usr/bin/env python3
"""Build the full checked original engine for iPad Simulator or device.

Run test-qol-native-bootstrap.py --enter-engine --probe-ntdll --prepare-only first.
Device builds bundle assets and require Apple development signing before installation.
"""
import sys,zipfile,argparse,hashlib,importlib.util,json,os,pathlib,plistlib,re,shutil,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
from checked_bochs_inputs import checked_bochs_inputs
from sdl_backend_inputs import inputs as sdl_inputs
from gl4es_backend_inputs import inputs as gl4es_inputs
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--platform',choices=['simulator','device','macos'],default='simulator')
ap.add_argument('--starsiege-object',type=pathlib.Path,help='Starsiege: Tribes for iOS (scripts/starsiege/build_engine.py --platform ...); adds its card and data')
ap.add_argument('--simulator-device')
ap.add_argument('--angle',action='store_true',help='Use the pinned ANGLE Metal renderer')
ap.add_argument('--cpu-host-checks',action='store_true',help='Validate native CPU spans and prove bounded native-object accesses; keep guest accesses checked')
ap.add_argument('--hardware-permissions',action='store_true',help='MMU-enforced checked memory: exact 4 KiB checks through page protections and fault emulation instead of per-access brackets')
ap.add_argument('--bundle-assets',action='store_true',help='Bundle game data (always enabled for device)')
ap.add_argument('--prepared-directory',type=pathlib.Path,default=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe')
ap.add_argument('--build-only',action='store_true')
a=ap.parse_args()
if a.platform=='device' and not a.build_only:ap.error('Device builds require --build-only; sign and install with your Apple development identity')
# The Mac app uses the same GL4ES/ANGLE (Metal) renderer as iOS.
if a.platform=='macos' and (not a.angle or not a.build_only):ap.error('Mac builds require --angle and --build-only')
mac=a.platform=='macos'
if a.platform=='simulator' and not a.build_only and not a.simulator_device:ap.error('--simulator-device is required to launch')
bundled=a.bundle_assets or a.platform in ('device','macos')
if a.hardware_permissions and a.cpu_host_checks:ap.error('--hardware-permissions replaces --cpu-host-checks')
source=a.prepared_directory.resolve();out=R/(dict(device='generated/ipad-engine-device',simulator='generated/ipad-engine',macos='generated/macos-engine')[a.platform]+('-hw' if a.hardware_permissions else ''));out.mkdir(exist_ok=True)
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
probe_source=next(p for p in sources if p.name=='ntdll-probes.ll');probe_text=probe_source.read_text()
for name,index in first_run.get('ntdll_probes',{}).items():
    probe='define hidden fastcc void @tbimp_'+name+'(ptr %cpu) {\n call void @qol_ntdll_probe(ptr %cpu, i32 '+str(index)+')\n unreachable\n}\n'
    assert probe_text.count(probe)==1,name
    probe_text=probe_text.replace(probe,'',1)
probe_wrapper=mobile_inputs/'ntdll-probes.ll';probe_wrapper.write_text(probe_text);sources[sources.index(probe_source)]=probe_wrapper
# The recorded probe source names its headers by the absolute path of the
# checkout it was generated in. Compile a copy that points at this checkout,
# so the build works in any folder on any Mac (GitHub's runner included).
probe_c=next(p for p in sources if p.name=='probe-native-providers.c')
portable=re.sub(r'#include "[^"]*/port/core/(\w+\.h)"',lambda m:'#include "%s"'%(R/'port/core'/m.group(1)),probe_c.read_text())
portable=portable.replace('#include "probe-native-providers.inc"','#include "%s"'%(probe_c.parent/'probe-native-providers.inc'))
probe_copy=mobile_inputs/probe_c.name;probe_copy.write_text(portable);sources[sources.index(probe_c)]=probe_copy
sources[sources.index(old_stops)]=wave_stops;sources.append(wave_wrapper)
# Refresh finite host exports without relifting unchanged original images.
from graphics_backend_inputs import prepare as graphics_inputs
graphics_sources,_=graphics_inputs(mobile_inputs)
old_wrapper=next(p for p in sources if p.name=='graphics-bindings.ll')
sources[sources.index(old_wrapper)]=next(p for p in graphics_sources if p.name=='graphics-bindings.ll')
# The generated no-op stubs for unprovided GL entry points (gl-unprovided.json).
sources.append(next(p for p in graphics_sources if p.name=='graphics-unprovided.c'))
# DirectSound COM routes are generated from dsound_backend_inputs.py, so methods
# implemented after the frozen preparation replace their stops.
from dsound_backend_inputs import prepare as dsound_inputs
dsound_sources,_=dsound_inputs(mobile_inputs)
old_dsound=next(p for p in sources if p.name=='dsound-bindings.ll')
sources[sources.index(old_dsound)]=next(p for p in dsound_sources if p.name=='dsound-bindings.ll')
sdk_name=dict(device='iphoneos',simulator='iphonesimulator',macos='macosx')[a.platform]
target='arm64-apple-macos12' if mac else 'arm64-apple-ios16'+('' if a.platform=='device' else '-simulator');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
sdl,sdl_flags,libraries=sdl_inputs(a.platform)
# GL4ES provides the GL entry points; never link Apple's desktop OpenGL beside it.
if mac:libraries=[x for i,x in enumerate(libraries) if not (x=='OpenGL' or (x=='-framework' and i+1<len(libraries) and libraries[i+1]=='OpenGL'))]
gl4es,gl_flags,gl_libraries=gl4es_inputs(a.platform);math=sse_inputs(a.platform)
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
earlier_text='// TribesPad: original offline mode. A dedicated server would quit the app, so host in game.\n$SkipLogin = true;\n$LaunchMode = "Offline";\n$Host::Dedicated = 0;\n'
# iOS and iPadOS end apps without the game's quit, which is where it saves
# preferences (warriors, accepted license). Save them as its onExit does.
offline_text=earlier_text+'// iOS and iPadOS close apps without a normal quit; save preferences as onExit does, once a minute.\nfunction TribesPadSavePrefs()\n{\n   export("$pref::*", "prefs/ClientPrefs.cs", False);\n   schedule(60000, 0, TribesPadSavePrefs);\n}\nschedule(60000, 0, TribesPadSavePrefs);\n'
# Settings > Online Play (on by default) starts the original TribesNEXT login;
# off, the app puts $TribesPadOffline = 1; before this script when it copies it
# into the profile (mobile_game_paths.m) and the game starts offline, with the internet server list in JOIN (GameGui.cs below).
# The login script asks for a 500 fps software limiter, an SDL timer the port
# does not provide; iOS paces frames with VSync, so the limiter stays off.
online_head="""// TribesPad: TribesNEXT online mode unless Settings > Online Play is off. A dedicated server would quit the app, so host in game.
if ( $TribesPadOffline )
{
   $SkipLogin = true;
   $LaunchMode = "Offline";
   $TribesPadInternetBrowser = true;
}
else
   $fromLauncher = true;
$Host::Dedicated = 0;
// Community maps bring interiors and textures players may not have. Turn on
// the TribesNEXT asset depot once (off by default on PC); the game's own
// option still switches it off.
if ( $pref::TribesPad::depotDefaulted $= "" )
{
   $pref::TribesPad::depotDefaulted = 1;
   $pref::Net::allowDownloads = true;
   $pref::Net::cacheDownloads = true;
}
// iOS paces frames with VSync; the optional software frame limiter is not available.
$pref::Engine::FramerateLimit = 0;
function setFramerateLimit( %fps )
{
}
"""
offline_text=online_head+offline_text[len(earlier_text):]
# The touch layer follows play, menus and text fields through this report
# (mobile_controls.m): "<playing> <typing>". Scheduled script events only run
# once the game is past the TribesNEXT login; the app handles that screen.
offline_text+="""// TribesPad: report whether the player is in play (PlayGui showing and the
// game cursor off) so the touch layer switches between touch controls and the
// menu cursor. Menus, the lobby, inventory and score screens turn the cursor on.
// Also report dialogs made for typing, so the on-screen keyboard opens with them.
function TribesPadInPlay()
{
   if ( !isObject( PlayGui ) || Canvas.getContent() != PlayGui.getId() )
      return 0;
   return !Canvas.isCursorOn();
}
function TribesPadTyping()
{
   for ( %i = 0; ( %dlg = getWord( "LoginDlg CreateAccountDlg PasswordDlg NewWarriorDlg EnterIPDlg FindServerDlg BrowserSearchDlg JoinChatDlg ChannelKeyDlg CreateTribeDlg DemoRenameFileDlg", %i ) ) !$= ""; %i++ )
      if ( isObject( %dlg ) && %dlg.isAwake() )
         return 1;
   return 0;
}
function TribesPadReportState()
{
   %state = TribesPadInPlay() SPC TribesPadTyping() SPC isObject( ServerConnection );
   if ( %state !$= $TribesPadLastState )
   {
      $TribesPadLastState = %state;
      %file = new FileObject();
      if ( %file.openForWrite( "prefs/tribespad-state.txt" ) )
      {
         %file.writeLine( %state );
         %file.close();
      }
      %file.delete();
   }
   TribesPadReadRequest();
   schedule( 200, 0, TribesPadReportState );
}
// TribesPad: requests from the app's ··· menu, one line "<number> <request>".
// Writing the file here first registers it with the game's file index; the
// app then rewrites it and each new number is acted on once.
function TribesPadStartRequests()
{
   $TribesPadRequest = "0";
   %file = new FileObject();
   if ( %file.openForWrite( "prefs/tribespad-request.txt" ) )
   {
      %file.writeLine( "0" );
      %file.close();
   }
   %file.delete();
}
function TribesPadReadRequest()
{
   %line = "";
   %file = new FileObject();
   if ( %file.openForRead( "prefs/tribespad-request.txt" ) )
   {
      %line = %file.readLine();
      %file.close();
   }
   %file.delete();
   %number = getWord( %line, 0 );
   if ( %number $= "" || %number $= $TribesPadRequest )
      return;
   $TribesPadRequest = %number;
   // ··· > Graphics: one of two detail presets, in a match or not.
   if ( getWord( %line, 1 ) $= "graphics" )
   {
      TribesPadGraphics( getWord( %line, 2 ) );
      return;
   }
   // Find a Game > Server List on a server: leave it (the lobby's own
   // Disconnect, after the app asked), then show JOIN.
   if ( getWord( %line, 1 ) $= "leave-servers" )
   {
      if ( isObject( ServerConnection ) )
         Disconnect();
      schedule( 500, 0, LaunchGame, "Join" );
      return;
   }
   // Find a Game: the original JOIN pane or a server, between matches only.
   if ( isObject( ServerConnection ) )
      return;
   switch$ ( getWord( %line, 1 ) )
   {
      case "servers":
         LaunchGame( "Join" );
      // Find a Game > Who's Playing Now > Join: the JOIN GAME button's own path.
      case "join":
         $JoinGamePassword = "";
         JoinGame( getWord( %line, 2 ) );
   }
}
// TribesPad: graphics presets. No card profile in CardProfiles.cs matches an
// Apple GPU, so the game never runs a detail profile and leaves these prefs
// empty; Options then saves the most expensive values (terrain screenError 0,
// texDetail 0). Shape detail and shape distance share one Options slider, and
// QoL widened its range (TS::detailAdjust up to 2.0), so each preset uses a
// pair that slider reproduces: Options keeps the values when it saves.
//   balanced: Dynamix High terrain, building and sky detail; slider at 0.5.
//   fast:     Dynamix Medium detail; slider near 0 (detail 0.68, as Medium).
function TribesPadDetail( %preset )
{
   %fast = %preset $= "fast";
   $pref::Terrain::screenError = %fast ? 20 : 8;
   $pref::Terrain::texDetail = %fast ? 3 : 1;
   $pref::TS::screenError = %fast ? 19 : 11;
   $pref::TS::detailAdjust = %fast ? 0.677778 : 1.3;
   $pref::Interior::detailAdjust = %fast ? 0.7 : 1.0;
   $pref::SkyOn = true;
   $pref::numCloudLayers = %fast ? 0 : 1;
   $pref::decalsOn = !%fast;
   $pref::environmentMaps = !%fast;
   $pref::Interior::ShowEnvironmentMaps = !%fast;
   $pref::Interior::DynamicLights = !%fast;
   $pref::precipitationOn = false;
   $pref::Shadows = %fast ? 0.1 : 0.25;
}
function TribesPadGraphics( %preset )
{
   if ( %preset !$= "fast" && %preset !$= "balanced" )
      return;
   TribesPadDetail( %preset );
   setShadowDetailLevel( $pref::Shadows );
   export( "$pref::*", "prefs/ClientPrefs.cs", False );
   echo( "TribesPad: applied the" SPC %preset SPC "graphics preset" );
}
// A new profile starts at Balanced instead of the empty detail prefs. This
// script runs twice at start-up, before and after the saved prefs load; the
// first run decides, so existing players keep their settings.
if ( $TribesPadFreshProfile $= "" )
   $TribesPadFreshProfile = !isFile( "prefs/ClientPrefs.cs" );
if ( $TribesPadFreshProfile )
   TribesPadDetail( "balanced" );
TribesPadStartRequests();
schedule( 200, 0, TribesPadReportState );
"""
if offline.exists():assert offline.read_text().startswith((earlier_text,'// TribesPad: offline mode','// TribesPad: TribesNEXT online mode')), 'Preserve custom autoexec.cs; select the launch mode there before building'
offline.write_text(offline_text)
# JOIN queries the internet master server in offline mode too (as prepare-macos-server-browser.py
# did for testing). The app refreshes this file in existing profiles (mobile_game_paths.m).
with zipfile.ZipFile(profile/'base/scripts.vl2') as archive:game_gui=archive.read('scripts/GameGui.cs')
for before,after in [(b'if ( isDemo() || $PlayingOnline )',b'if ( isDemo() || $PlayingOnline || $TribesPadInternetBrowser )'),
    (b'GMJ_Browser.lastQuery = $PlayingOnline ? "Master" : "LanServers";',b'GMJ_Browser.lastQuery = ($PlayingOnline || $TribesPadInternetBrowser) ? "Master" : "LanServers";')]:
    assert game_gui.count(before)==1;game_gui=game_gui.replace(before,after)
(profile/'base/scripts').mkdir(exist_ok=True);(profile/'base/scripts/GameGui.cs').write_bytes(game_gui)

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
app=out/'TribesPad.app';app.mkdir(exist_ok=True)
# Mac bundles keep code in Contents/MacOS and data in Contents/Resources.
contents=app/'Contents' if mac else app;resources=contents/'Resources' if mac else app
# On the Mac the game picker (launcher_mac.m) is the app's executable and execs
# the chosen engine; Tribes 2's engine is TribesPadEngine beside it.
exe=(contents/'MacOS' if mac else app)/('TribesPadEngine' if mac else 'TribesPad');exe.parent.mkdir(parents=True,exist_ok=True);resources.mkdir(parents=True,exist_ok=True)
if not a.angle:
 # Remove only this backend's owned files when switching back to EAGL.
 for name in ('libEGL.framework','libGLESv2.framework'):
  framework=app/'Frameworks'/name
  if framework.exists():shutil.rmtree(framework)
 license=app/'LICENSE.ANGLE'
 if license.exists():license.unlink()
shutil.copy2(R/'generated/gl4es'/a.platform/'LICENSE.gl4es',resources/'LICENSE.gl4es')
args=[str(exe),str(source/'engine.bin'),str(source/'qol.bin'),str(volume),str(profile),str(temp),str(capture),'engine']
if bundled:
 data=resources/'GameData';data.mkdir(exist_ok=True)
 # The game picker's art: TribesPad's emblem and each game's own title screen,
 # taken from the player's own downloads (scripts/launcher_art.py); nothing from
 # the games is in the repository. Without a title screen, Tribes 2 falls back
 # to a menu background from its own files and Starsiege to TribesPad's art.
 launcher_art=data/'Launcher';launcher_art.mkdir(exist_ok=True)
 for old in launcher_art.glob('*.png'):old.unlink()
 shutil.copy2(R/'docs/images/tribespad-logo.png',launcher_art/'emblem.png')
 from launcher_art import prepare as prepare_launcher_art
 if 'tribes2' not in prepare_launcher_art(launcher_art):
  with zipfile.ZipFile(profile/'base/textures.vl2') as archive:(launcher_art/'tribes2.png').write_bytes(archive.read('textures/gui/bg_Starwolf.png'))
 # Incremental rsync avoids recopying hundreds of MB on native-code-only builds.
 # The isolated source profile is copied without following external symlinks.
 assert not any(p.is_symlink() for p in profile.rglob('*')), 'Bundled profile must not contain symlinks'
 subprocess.run(['rsync','-a','--delete',str(profile)+'/',str(data/'Profile')+'/'],check=True)
 loader=data/'Loader';loader.mkdir(exist_ok=True)
 for name in ['published-code.bin',*image_sources]:shutil.copy2(capture/name,loader/name)
 for name in ['engine.bin','qol.bin']:shutil.copy2(source/name,data/name)
 config='#define TB_BUNDLED_GAME_DATA 1\n'
 if a.starsiege_object and not mac:
  # Starsiege: Tribes, one object exporting only starsiege_mobile_engine_main.
  sys.path.insert(0,str(R/'scripts/starsiege'))
  from bundle_mac import add_game_data
  add_game_data(data);config+='#define TB_STARSIEGE 1\n'
 elif not mac and (data/'Starsiege').exists():shutil.rmtree(data/'Starsiege')
else:
 config='#define TB_BUNDLED_GAME_DATA 0\nstatic const char *launch_workdir='+json.dumps(str(R))+';\nstatic char *launch_arguments[]={'+','.join(json.dumps(p) for p in args)+',NULL};\n'
(out/'mobile-engine-launch-config.h').write_text(config)
host=[R/'port/apple/mobile_engine_main.m',R/'port/apple/mobile_game_paths.m',R/'port/apple/sdl_main.c',R/'port/apple/gl4es_context.c',R/'port/core/input.c']
host+=[R/'port/apple/desktop_controls.c'] if mac else [R/'port/apple/mobile_controls.m',R/'port/apple/launcher.m',R/'port/apple/starsiege_paths.m',R/'port/apple/mobile_controls.c',R/'port/apple/live_servers.m',R/'port/apple/content_area.m']
if a.angle:host.append(R/'port/apple/angle_sdl.c')
command=['clang','-target',target,'-isysroot',sdk,'-O2','-I',str(out),*sdl_flags,*gl_flags,'-DNOX11','-DNOEGL','-DNO_LOADER','-DDEFAULT_ES=2','-isystem',str(R/'ref/gl4es/src'),*map(str,host),*objects,*([str(a.starsiege_object.resolve()),'-framework','ImageIO'] if a.starsiege_object and not mac else []),*gl_libraries,*libraries,*math['libraries'],*[str(R/n) for n in http['libraries']],str(R/al['library']),str(zbuild/'libz-ng.a'),'-lz','-lc++','-framework','Foundation','-framework','GameController','-framework','AVFoundation','-framework','CoreGraphics','-framework','CoreText','-framework','CoreFoundation','-framework','SystemConfiguration','-framework','AudioToolbox','-framework','CoreAudio',*(['-framework','AppKit','-framework','Metal','-framework','QuartzCore'] if mac else ['-framework','SafariServices']),'-o',str(exe)]
command[1:1]=['-fobjc-arc']
with (out/'link.log').open('w') as log:subprocess.run(command,stdout=log,stderr=log,check=True)
identifier='com.tribespad.full-engine.ipad'
if mac:
 picker=contents/'MacOS'/'TribesPad'
 subprocess.run(['clang','-target',target,'-isysroot',sdk,'-fobjc-arc','-O2','-Wall','-Wextra','-Werror',str(R/'port/apple/launcher_mac.m'),str(R/'port/apple/starsiege_paths.m'),'-framework','AppKit','-framework','QuartzCore','-framework','GameController','-o',str(picker)],check=True)
from app_icon import compile_icon
if mac:
 icon_keys=compile_icon(resources,'macos',minimum='12.0')
 (contents/'Info.plist').write_bytes(plistlib.dumps(dict(**icon_keys,CFBundleDisplayName='TribesPad',CFBundleIdentifier='com.tribespad.app',CFBundleExecutable='TribesPad',CFBundleName='TribesPad',CFBundlePackageType='APPL',CFBundleVersion='1',CFBundleShortVersionString='0.1',LSMinimumSystemVersion='12.0',NSHighResolutionCapable=True,LSApplicationCategoryType='public.app-category.action-games',NSLocalNetworkUsageDescription='Find and connect to local Tribes 2 games.')))
else:
 icon_keys=compile_icon(app,a.platform,families=('iphone','ipad'))
 (app/'Info.plist').write_bytes(plistlib.dumps(dict(**icon_keys,CFBundleDisplayName='TribesPad',CFBundleIdentifier=identifier,CFBundleExecutable='TribesPad',CFBundleName='TribesPad',CFBundlePackageType='APPL',CFBundleSupportedPlatforms=['iPhoneOS' if a.platform=='device' else 'iPhoneSimulator'],CFBundleVersion='1',CFBundleShortVersionString='0.1',MinimumOSVersion='16.0',UIDeviceFamily=[1,2],UIRequiresFullScreen=True,LSRequiresIPhoneOS=True,UIRequiredDeviceCapabilities=['arm64'],NSLocalNetworkUsageDescription='Find and connect to local Tribes 2 games.',NSMicrophoneUsageDescription='Tribes 2 voice chat uses the microphone while you hold the voice key.',UIApplicationSupportsIndirectInputEvents=True,UIFileSharingEnabled=True,LSSupportsOpeningDocumentsInPlace=True,UIApplicationSceneManifest=dict(UIApplicationSupportsMultipleScenes=False,UISceneConfigurations=dict(UIWindowSceneSessionRoleApplication=[dict(UISceneConfigurationName='TribesPad',UISceneDelegateClassName='TBSceneDelegate')])),UILaunchScreen={},UISupportedInterfaceOrientations=['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'])))
signing=['--entitlements',str(R/'port/apple/TribesPad.entitlements')] if a.platform=='device' else []
if a.angle:
 for framework in angle_bundle(app,a.platform):subprocess.run(['codesign','--force','--sign','-',str(framework)],check=True)
if mac:subprocess.run(['codesign','--force','--sign','-',str(exe)],check=True)
subprocess.run(['codesign','--force','--sign','-',*signing,str(app)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
(out/'result.json').write_text(json.dumps(dict(state='built',platform=a.platform,cpu_host_checks=a.cpu_host_checks,hardware_permissions=a.hardware_permissions,bundled_assets=bundled,entitlements_sha256=sha(R/'port/apple/TribesPad.entitlements') if a.platform=='device' else None,binary_sha256=sha(exe),prepared_sha256=sha(source/'prepared-inputs.json'),support_sha256=sha(out/'support/result.json'),guarded_sha256=sha(out/'guarded/result.json'),gl4es=gl4es,angle=angle,openal=al,arguments=None if bundled else args,mobile_hooks_sha256=sha(mobile_inputs/'mobile-hooks.json'),x87_stores_sha256=sha(mobile_inputs/'x87-stores.json'),mp3_stores_sha256=sha(mobile_inputs/'mp3-stores.json'),wav_io_test_sha256=sha(wave_proof_path),mobile_gpu_timing=dict(available=False,original_blocks=timing_blocks,test_sha256=sha(timing_proof),translated_sha256=sha(mobile_qol)),scope='Full checked engine linked for '+a.platform+'. Device builds need development signing. Build is not rendering or gameplay acceptance.'),indent=2)+'\n')
if not a.build_only:
 subprocess.run(['xcrun','simctl','install',a.simulator_device,str(app)],check=True)
 for name in ('stdout.log','stderr.log'):(out/name).write_text('')
 subprocess.run(['xcrun','simctl','launch','--stdout='+str(out/'stdout.log'),'--stderr='+str(out/'stderr.log'),a.simulator_device,identifier],check=True)
print(app)
