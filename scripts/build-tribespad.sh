#!/usr/bin/env bash
# Build TribesPad on an Apple Silicon Mac from the official free downloads.
#
#   scripts/build-tribespad.sh [--platform device|macos|simulator] [--output DIR] [--ipa FILE] [--zip FILE]
#
# Downloads Tribes 2 and the TribesNEXT client, the pinned open-source
# dependencies and tools, translates the game on this Mac and builds the app.
# --platform device (default): an unsigned TribesPad.ipa for iPhone and iPad;
# sign it with your own Apple account (see README).
# --platform macos: TribesPad-mac.zip with TribesPad.app for Apple Silicon Macs.
# Needs Xcode and: brew install llvm@20 cmake ninja sevenzip zstd
set -euo pipefail

platform=device
output=
ipa=
zip=
while [ $# -gt 0 ]; do
  case "$1" in
    --platform) platform="$2"; shift 2 ;;
    --output) output="$2"; shift 2 ;;
    --ipa) ipa="$2"; shift 2 ;;
    --zip) zip="$2"; shift 2 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
case "$platform" in device|simulator|macos) ;; *) echo "--platform must be device, macos or simulator" >&2; exit 2 ;; esac

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"
output="${output:-$repo/build/output}"
mkdir -p "$output"
output="$(cd "$output" && pwd)"
ipa="${ipa:-$output/TribesPad.ipa}"
case "$ipa" in /*) ;; *) ipa="$PWD/$ipa" ;; esac

step() { printf '\n==> %s\n' "$*"; }
fail() { printf 'build-tribespad: %s\n' "$*" >&2; exit 1; }

[ "$(uname -m)" = arm64 ] || fail "an Apple Silicon Mac is required"
if [ -z "${DEVELOPER_DIR:-}" ]; then
  selected="$(xcode-select -p 2>/dev/null || true)"
  case "$selected" in
    *.app/Contents/Developer) export DEVELOPER_DIR="$selected" ;;
    *) if [ -d /Applications/Xcode.app ]; then export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer; fi ;;
  esac
fi
if [ "$platform" != macos ]; then
  xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1 || fail "Xcode with the iOS platform is required"
fi
xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1 || fail "Xcode is required"
for tool in zstd cmake ninja 7zz; do
  command -v "$tool" >/dev/null || fail "$tool is missing: brew install llvm@20 cmake ninja sevenzip zstd"
done
[ -x /opt/homebrew/Cellar/llvm@20/20.1.8/bin/clang ] || fail "LLVM 20.1.8 is missing: brew install llvm@20"

sdk() { xcrun --sdk "$1" --show-sdk-path; }
mac_sdk="$(sdk macosx)"
case "$platform" in device) ios_sdk="$(sdk iphoneos)" ;; simulator) ios_sdk="$(sdk iphonesimulator)" ;; macos) ios_sdk="$mac_sdk" ;; esac

step "Python environment"
if [ ! -x .venv/bin/python ]; then
  python=/usr/bin/python3
  [ -x "$python" ] || python="$(command -v python3)"
  "$python" -m venv .venv
fi
.venv/bin/python -m pip install -q --disable-pip-version-check -r scripts/requirements.txt
py=.venv/bin/python

step "Game files (official Tribes 2 installer and TribesNEXT client)"
SDKROOT="$mac_sdk" $py scripts/prepare-game-inputs.py

step "Open-source dependencies"
$py scripts/fetch-sources.py

step "Lifter tools"
SDKROOT="$mac_sdk" $py scripts/build-tools.py

step "Translating the game (lift and patch; about 10 minutes)"
SDKROOT="$mac_sdk" $py scripts/translation-patches.py bases
$py scripts/translation-patches.py apply

step "Libraries for the macOS build tools"
for script in build-native-openal build-http-backend build-softfloat; do
  SDKROOT="$mac_sdk" $py "scripts/$script.py" --platform macos
done

step "Libraries for $platform"
for script in build-softfloat build-bochs-math build-sdl-backend build-gl4es-mobile build-native-openal build-zlib-ng build-http-backend; do
  # The macOS build-tool libraries above already cover these three.
  if [ "$platform" = macos ]; then case "$script" in build-softfloat|build-native-openal|build-http-backend) continue ;; esac; fi
  SDKROOT="$ios_sdk" $py "scripts/$script.py" --platform "$platform"
done
SDKROOT="$ios_sdk" $py scripts/build-checked-bochs.py --platform "$platform" --hardware

step "Game profile"
$py scripts/apple_game_profile.py

step "TribesPad app"
SDKROOT="$ios_sdk" $py scripts/build-ipad-engine.py --platform "$platform" --angle --hardware-permissions --bundle-assets --build-only
case "$platform" in
  device) app="$repo/generated/ipad-engine-device-hw/TribesPad.app" ;;
  simulator) app="$repo/generated/ipad-engine-hw/TribesPad.app" ;;
  macos) app="$repo/generated/macos-engine-hw/TribesPad.app" ;;
esac

step "Package"
if [ "$platform" = device ]; then
  stage="$(mktemp -d)"
  mkdir "$stage/Payload"
  ditto "$app" "$stage/Payload/TribesPad.app"
  mkdir -p "$(dirname "$ipa")"
  (cd "$stage" && zip -qry "$ipa" Payload)
  printf '\nBuilt %s\nSign it with your own Apple account to install it (README: Install).\n' "$ipa"
elif [ "$platform" = macos ]; then
  zip_path="${zip:-$output/TribesPad-mac.zip}"
  ditto -c -k --sequesterRsrc --keepParent "$app" "$zip_path"
  printf '\nBuilt %s\nUnzip it and move TribesPad to Applications.\n' "$zip_path"
else
  ditto "$app" "$output/TribesPad.app"
  printf '\nBuilt %s for the iOS Simulator.\n' "$output/TribesPad.app"
fi

