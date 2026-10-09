#!/usr/bin/env python3
"""Exercise bundled app storage and compile both mobile launch configurations."""
import hashlib,json,os,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/mobile-game-paths';out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
def sdk(name):return subprocess.check_output(['xcrun','--sdk',name,'--show-sdk-path'],env=env,text=True).strip()
common=['xcrun','clang','-fobjc-arc','-Wall','-Wextra','-Werror']
subprocess.run(common+['-isysroot',sdk('macosx'),'-framework','Foundation','port/apple/mobile_game_paths.m','tests/mobile_game_paths_test.m','-o',str(out/'test')],cwd=R,env=env,check=True)
result=subprocess.run([str(out/'test')],capture_output=True,text=True,env=env,check=True)
print(result.stdout,end='')
for platform,target in [('simulator','arm64-apple-ios16-simulator'),('device','arm64-apple-ios16')]:
    flags=['-target',target,'-isysroot',sdk('iphoneos' if platform=='device' else 'iphonesimulator')]
    subprocess.run(common+flags+['-c','port/apple/mobile_game_paths.m','-o',str(out/(platform+'-paths.o'))],cwd=R,env=env,check=True)
    for bundled in (0,1):
        config='#define TB_BUNDLED_GAME_DATA '+str(bundled)+'\n'
        if not bundled:config+='static const char *launch_workdir="/tmp";\nstatic char *launch_arguments[9];\n'
        (out/'mobile-engine-launch-config.h').write_text(config)
        subprocess.run(common+flags+['-I',str(out),'-Igenerated/sdl-backend/'+platform+'/install/include','-c','port/apple/mobile_engine_main.m','-o',str(out/(platform+'-entry-'+str(bundled)+'.o'))],cwd=R,env=env,check=True)
sources=['scripts/test-mobile-game-paths.py','port/apple/mobile_game_paths.h','port/apple/mobile_game_paths.m','port/apple/mobile_engine_main.m','tests/mobile_game_paths_test.m']
(out/'result.json').write_text(json.dumps(dict(passed=True,output=result.stdout,expected_failure=result.stderr,sources_sha256={s:hashlib.sha256((R/s).read_bytes()).hexdigest() for s in sources},scope='Native Foundation app-storage execution and Simulator/device entry compilation; not physical-device execution.'),indent=2)+'\n')
