#!/usr/bin/env bash
# Sign the device build with your Apple development identity and install it on a
# connected iPhone or iPad.
#
#   scripts/install-device.sh --identity "Apple Development: Name (ID)" \
#       --profile PROFILE.mobileprovision --device UDID [--app PATH]
#
# The profile must cover the app's bundle identifier (a team wildcard works).
# Extended Virtual Addressing is requested only when the profile grants it.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
app="$repo/generated/ipad-engine-device-hw/TribesPad.app"
identity= profile= device=
while [ $# -gt 0 ]; do
  case "$1" in
    --identity) identity="$2"; shift 2 ;;
    --profile) profile="$2"; shift 2 ;;
    --device) device="$2"; shift 2 ;;
    --app) app="$2"; shift 2 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[ -n "$identity" ] && [ -n "$profile" ] && [ -n "$device" ] || { sed -n '2,9p' "$0"; exit 2; }
[ -f "$app/Info.plist" ] || { echo "no device build at $app" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
security cms -D -i "$profile" > "$work/profile.plist"
team="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:com.apple.developer.team-identifier' "$work/profile.plist")"
granted="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:application-identifier' "$work/profile.plist")"
bundle="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Info.plist")"
case "$team.$bundle" in $granted) ;; *) echo "profile $granted does not cover $bundle" >&2; exit 1 ;; esac

stage="$work/TribesPad.app"
ditto "$app" "$stage"
cp "$profile" "$stage/embedded.mobileprovision"
cat > "$work/entitlements.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>application-identifier</key><string>$team.$bundle</string>
<key>com.apple.developer.team-identifier</key><string>$team</string>
<key>get-task-allow</key><true/>
</dict></plist>
EOF
if /usr/libexec/PlistBuddy -c 'Print :Entitlements:com.apple.developer.kernel.extended-virtual-addressing' "$work/profile.plist" >/dev/null 2>&1; then
  /usr/libexec/PlistBuddy -c 'Add :com.apple.developer.kernel.extended-virtual-addressing bool true' "$work/entitlements.plist"
else
  echo "note: this profile has no Extended Virtual Addressing; signing without it"
fi
for framework in "$stage"/Frameworks/*; do
  [ -e "$framework" ] && codesign --force --sign "$identity" --timestamp=none "$framework"
done
codesign --force --sign "$identity" --entitlements "$work/entitlements.plist" --timestamp=none "$stage"
codesign --verify --deep --strict "$stage"
xcrun devicectl device install app --device "$device" "$stage"
echo "Installed $bundle on $device"
