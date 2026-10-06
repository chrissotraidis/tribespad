#!/usr/bin/env python3
"""Build the native Apple diagnostic shell. Engine integration remains a separate gate."""
import argparse,json,pathlib,plistlib,subprocess,hashlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--platform',choices=['simulator','macos','device'],default='simulator');p.add_argument('--simulator',help='Boot/install/launch on this one simulator UUID');a=p.parse_args()
if a.simulator and a.platform!='simulator':raise SystemExit('--simulator requires --platform simulator')
subprocess.run([str(ROOT/'.venv/bin/python'),'scripts/lift-slices.py'],cwd=ROOT,check=True)
sdk={'simulator':'iphonesimulator','macos':'macosx','device':'iphoneos'}[a.platform]
sdkpath=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
target={'simulator':'arm64-apple-ios17.0-simulator','macos':'arm64-apple-macos13.0','device':'arm64-apple-ios17.0'}[a.platform]
app=ROOT/'generated/apple'/a.platform/'TribesPad.app';app.mkdir(parents=True,exist_ok=True)
binpath=app/'TribesPad';plistpath=app/'Info.plist'
if a.platform=='macos':
 binpath=app/'Contents/MacOS/TribesPad';binpath.parent.mkdir(parents=True,exist_ok=True);plistpath=app/'Contents/Info.plist'
info={'CFBundleIdentifier':'com.tribespad.client','CFBundleName':'TribesPad','CFBundleExecutable':'TribesPad','CFBundlePackageType':'APPL','CFBundleVersion':'1','CFBundleShortVersionString':'0.1','NSLocalNetworkUsageDescription':'TribesPad connects to your Tribes 2 multiplayer servers.'}
if a.platform=='macos':info.update({'LSMinimumSystemVersion':'13.0','NSHighResolutionCapable':True})
else:info.update({'MinimumOSVersion':'17.0','UIDeviceFamily':[1,2],'UISupportedInterfaceOrientations':['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'],'UISupportedInterfaceOrientations~ipad':['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'],'UILaunchScreen':{},'UIRequiresFullScreen':True,'UIStatusBarHidden':True})
plistpath.write_bytes(plistlib.dumps(info))
cmd=['xcrun','--sdk',sdk,'clang','-target',target,'-isysroot',sdkpath,'-O2','-fobjc-arc','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-label','-Wno-deprecated-declarations','-I',str(ROOT/'port/core'),str(ROOT/'port/apple/TribesPad.m'),str(ROOT/'port/core/input.c'),str(ROOT/'port/core/server_query.c'),str(ROOT/'generated/slices/tribes_slices.c'),'-framework','Foundation','-framework','CoreGraphics','-framework','QuartzCore','-framework','GameController','-framework',('AppKit' if a.platform=='macos' else 'UIKit'),'-o',str(binpath)]
r=subprocess.run(cmd,capture_output=True,text=True);(app.parent/'build.log').write_text(r.stdout+r.stderr)
if r.returncode:print(r.stderr);raise SystemExit(r.returncode)
subprocess.run(['codesign','--force','--sign','-',str(app)],check=True,capture_output=True)
(app.parent/'build.json').write_text(json.dumps({'platform':a.platform,'target':target,'binary_sha256':hashlib.sha256(binpath.read_bytes()).hexdigest(),'gameplay_ready':False,'self_test':['original build getter','original CRC32'],'sources':[str(x.relative_to(ROOT)) for x in [ROOT/'port/apple/TribesPad.m',ROOT/'port/core/input.c']]},indent=2)+'\n')
if a.platform=='macos':subprocess.run([str(binpath),'--self-test'],check=True)
if a.simulator:
 state=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json']))
 other=[d['udid'] for group in state['devices'].values() for d in group if d['udid']!=a.simulator]
 if other:raise SystemExit('Another simulator is booted; shut it down explicitly before sequential testing: '+','.join(other))
 device=next((d for group in json.loads(subprocess.check_output(['xcrun','simctl','list','devices','--json']))['devices'].values() for d in group if d['udid']==a.simulator),None)
 if not device:raise SystemExit('Unknown simulator UUID')
 if device['state']!='Booted':subprocess.run(['xcrun','simctl','boot',a.simulator],check=True)
 subprocess.run(['xcrun','simctl','bootstatus',a.simulator,'-b'],check=True)
 subprocess.run(['xcrun','simctl','terminate',a.simulator,'com.tribespad.client'],capture_output=True)
 subprocess.run(['xcrun','simctl','install',a.simulator,str(app)],check=True)
 subprocess.run(['xcrun','simctl','launch',a.simulator,'com.tribespad.client'],check=True)
print(app)
