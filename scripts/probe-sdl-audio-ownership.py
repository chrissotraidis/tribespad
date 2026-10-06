#!/usr/bin/env python3
"""Isolated audio ownership experiment; never enables AUDIO in production."""
import argparse,hashlib,importlib.util,json,pathlib,plistlib,subprocess
from sdl_backend_inputs import inputs
ap=argparse.ArgumentParser();ap.add_argument('--baseline',action='store_true');a=ap.parse_args()
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review'/('audio-baseline-reproduction' if a.baseline else 'audio-native-ownership');out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();backend,flags,libraries=inputs();original=R/('generated/integration-review/display-immediates-checkpoint-20261003/port/core/guest_sdl_backend.c' if a.baseline else 'port/core/guest_sdl_backend.c');candidate=out/'backend.c';s=original.read_text();s=s.replace('SDL_INIT_EVENTS|SDL_INIT_VIDEO)','SDL_INIT_EVENTS|SDL_INIT_VIDEO|SDL_INIT_AUDIO)');assert 'SDL_INIT_EVENTS|SDL_INIT_VIDEO|SDL_INIT_AUDIO' in s
s+='''
int review_audio_probe(_cpu *cpu,int playback) {
 NativeFrame frame;if(!enter(cpu,&frame))abort();
 if(!SDL_InitSubSystem(SDL_INIT_AUDIO)){fprintf(stderr,"AUDIO_INIT_FAILED %s\\n",SDL_GetError());leave(&frame);return 1;}
 fprintf(stderr,"AUDIO_INIT_OK driver=%s\\n",SDL_GetCurrentAudioDriver());
 SDL_AudioDeviceID dev=0;SDL_AudioStream *stream=NULL;
 if(playback){
  SDL_AudioSpec spec={SDL_AUDIO_F32,2,48000};dev=SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec);
  if(!dev){fprintf(stderr,"AUDIO_OPEN_FAILED %s\\n",SDL_GetError());leave(&frame);return 2;}
  stream=SDL_CreateAudioStream(&spec,&spec);if(!stream||!SDL_BindAudioStream(dev,stream))abort();
  float silence[4800]={0};if(!SDL_PutAudioStreamData(stream,silence,sizeof silence))abort();
  fprintf(stderr,"AUDIO_STREAM_QUEUED\\n");
 }
 leave(&frame);SDL_Delay(250);
 if(!enter(cpu,&frame))abort();
 if(stream)SDL_DestroyAudioStream(stream);if(dev)SDL_CloseAudioDevice(dev);
 SDL_QuitSubSystem(SDL_INIT_AUDIO);leave(&frame);fprintf(stderr,"AUDIO_SHUTDOWN_OK\\n");return 0;
}
''';s=s.replace('NativeFrame frame','TribesCurlFrame frame') if a.baseline else s;candidate.write_text(s)
t=R/'tests/sdl_video_test.c';h=t.read_text();h=h[:h.index(' strcpy((char *)memory+0x2000,"tribespad-invalid-driver");')]+''' assert(call(18,SDL_INIT_VIDEO,0));
 int result=review_audio_probe(&cpu,argc>1&&!strcmp(argv[1],"playback"));
 call(19,0,0);fprintf(stderr,"PROBE_RESULT %d allocations=%u frees=%u live=%u\\n",result,sdl_test_allocations,sdl_test_frees,sdl_test_live);return result;
}
int main(int argc,char **argv){return tribes_sdl_run_application(argc,argv,video_main);}
''';h=h.replace('#include "../port/','#include "'+str(R/'port')+'/').replace('static void *wrong_thread','extern int review_audio_probe(_cpu *,int);\nstatic void *wrong_thread');host=out/'probe.c';host.write_text(h)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=json.loads((R/'config/runtime/sdl-bindings.json').read_text())
for name,helper,n,returns in [('TestMalloc','sdl_test_malloc',1,True),('TestCalloc','sdl_test_calloc',2,True),('TestRealloc','sdl_test_realloc',2,True),('TestFree','sdl_test_free',1,False),('TestLog','sdl_test_log',4,False)]:bindings[name]=dict(helper=helper,arguments=n,returns=returns,guest_base=False,cpu_argument=True,calling_convention='cdecl')
wrapper=out/'bindings.ll';wrapper.write_text(gen.generate(bindings,False));sources=[R/n for n in ['port/apple/sdl_main.c','port/core/guest_sdl_sync.c','port/core/guest_sdl_tls.c','port/core/guest_sdl_memory.c','port/core/guest_curl_allocator.c','port/core/guest_callback.c','port/core/guest_heap.c','port/core/process_environment.c','port/core/guest_memory_checks.c','port/core/guest_external_checks.c','tests/fixtures/sdl_hint_dispatch.c','tests/fixtures/native_enter.ll']]+[candidate,host,wrapper]
app=out/'AudioOwnershipProbe.app';exe=app/'Contents/MacOS/AudioOwnershipProbe';exe.parent.mkdir(parents=True,exist_ok=True);(app/'Contents/Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='com.tribespad.audio-ownership-review',CFBundleExecutable=exe.name,CFBundlePackageType='APPL',LSMinimumSystemVersion='12.0')))
subprocess.run(['clang','-target','arm64-apple-macos12','-isysroot',backend['sdk'],'-DPTROFS_64BIT','-O1','-g','-Wno-override-module','-I',str(R/'port/core'),*flags,*map(str,sources),*libraries,'-o',str(exe)],check=True);subprocess.run(['codesign','--force','--sign','-',str(app)],check=True,capture_output=True)
runs=[]
for mode in ['init','playback']:
 try:
  run=subprocess.run([str(exe),mode],capture_output=True,text=True,timeout=15);output=run.stdout+run.stderr;code=run.returncode
 except subprocess.TimeoutExpired as e:output=(e.stdout or b'').decode()+(e.stderr or b'').decode();code='timeout'
 (out/(mode+'.log')).write_text(output);runs.append(dict(mode=mode,exit=code,output=output));print(mode,code,output.strip(),flush=True)
(out/'result.json').write_text(json.dumps(dict(experiment_only=True,production_audio_enabled=not a.baseline,runs=runs,binary_sha256=sha(exe),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[original,t,R/'config/runtime/sdl-bindings.json',pathlib.Path(__file__).resolve()]},scope='Native CoreAudio initialization and silent stream under an isolated copy of the current SDL owner bridge. Reveals integration failures; not guest audio ABI, audible output, or production acceptance.'),indent=2)+'\n')
