"""Compile the TribesPad app icon (port/apple/Assets.xcassets) into an app bundle.

Returns the Info.plist keys actool generates (CFBundleIcons / CFBundleIconFile).
The icon is a full-bleed 1024 px square; iOS, iPadOS and macOS apply their own
corner shapes, so the artwork has no rounded corners or transparency.
"""
import pathlib,plistlib,subprocess,tempfile
R=pathlib.Path(__file__).resolve().parents[1]
CATALOG=R/'port/apple/Assets.xcassets'
def compile_icon(resources,platform,families=('ipad',),minimum='16.0'):
 sdk={'device':'iphoneos','simulator':'iphonesimulator','macos':'macosx'}[platform]
 resources=pathlib.Path(resources);resources.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory() as tmp:
  partial=pathlib.Path(tmp)/'icon.plist'
  command=['xcrun','actool',str(CATALOG),'--compile',str(resources),'--platform',sdk,
   '--minimum-deployment-target',minimum,'--app-icon','AppIcon','--output-partial-info-plist',str(partial),
   '--errors','--warnings','--output-format','human-readable-text']
  for family in ([] if platform=='macos' else families):command+=['--target-device',family]
  subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
  return plistlib.loads(partial.read_bytes())
