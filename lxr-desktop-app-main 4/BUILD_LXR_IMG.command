#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

say() { printf '\n%s\n' "$*"; }
fail() { say "ERROR: $*"; read -r -p 'Press Enter to close...' _; exit 1; }

if [[ "$(uname -s)" != "Darwin" ]]; then
  fail 'This one-click builder is intended for macOS.'
fi

if ! command -v brew >/dev/null 2>&1; then
  fail 'Homebrew is not installed. Install Homebrew first, then double-click this file again.'
fi

say 'Checking build tools...'
NEED_CMAKE=0
if ! command -v cmake >/dev/null 2>&1; then
  NEED_CMAKE=1
else
  CMAKE_VERSION="$(cmake --version | awk 'NR==1 {print $3}')"
  CMAKE_MAJOR="${CMAKE_VERSION%%.*}"
  CMAKE_MINOR="${CMAKE_VERSION#*.}"
  CMAKE_MINOR="${CMAKE_MINOR%%.*}"
  if (( CMAKE_MAJOR < 3 || (CMAKE_MAJOR == 3 && CMAKE_MINOR < 21) )); then NEED_CMAKE=1; fi
fi
if (( NEED_CMAKE )); then brew install cmake; fi
if ! command -v ninja >/dev/null 2>&1; then
  brew install ninja
fi
if ! command -v python3 >/dev/null 2>&1; then
  brew install python
fi

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  say 'Installing the official Arm GNU Toolchain (one-time installation)...'
  brew install --cask gcc-arm-embedded
fi

ARM_GCC="$(command -v arm-none-eabi-gcc || true)"
if [[ -z "$ARM_GCC" ]]; then
  for candidate in /Applications/ArmGNUToolchain/*/arm-none-eabi/bin/arm-none-eabi-gcc; do
    if [[ -x "$candidate" ]]; then ARM_GCC="$candidate"; break; fi
  done
fi
[[ -n "$ARM_GCC" && -x "$ARM_GCC" ]] || fail 'arm-none-eabi-gcc was not found after toolchain installation.'
ARM_BIN="$(dirname "$ARM_GCC")"

say "Using compiler: $ARM_GCC"
"$ARM_GCC" --version | head -n 1
say 'Preparing a clean release build...'
rm -rf "$ROOT/build/release"

cmake --preset release -DARM_TOOLCHAIN_DIR="$ARM_BIN"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
cmake --build --preset release --parallel "$JOBS"

IMG="$ROOT/build/release/LXR_V2_V1.7.img"
[[ -s "$IMG" ]] || fail 'Build finished but LXR_V2_V1.7.img was not created.'

say 'Verifying LXR update image header, size and checksum...'
python3 - "$IMG" <<'PY'
import struct, sys
path = sys.argv[1]
data = open(path, 'rb').read()
if len(data) < 17:
    raise SystemExit('Image is too short')
magic, size, checksum = struct.unpack('<8sIB', data[:13])
payload = data[16:]
if magic != b'LXRV2IMG':
    raise SystemExit(f'Wrong image magic: {magic!r}')
if size != len(payload):
    raise SystemExit(f'Size mismatch: header={size}, actual={len(payload)}')
expected = 0xFF - (sum(payload) & 0xFF)
if checksum != expected:
    raise SystemExit(f'Checksum mismatch: header={checksum:#04x}, expected={expected:#04x}')
print(f'OK: {path}')
print(f'Firmware payload: {size} bytes; checksum: {checksum:#04x}')
PY

shasum -a 256 "$IMG"
say "Firmware image is ready: $IMG"
read -r -p 'To copy it to the LXR-02 SD-card root, enter its mounted path (for example /Volumes/LXR02). Press Enter to skip: ' TARGET
if [[ -n "$TARGET" ]]; then
  TARGET="${TARGET/#\~/$HOME}"
  TARGET="${TARGET%/}"
  [[ -d "$TARGET" ]] || fail "Folder does not exist: $TARGET"
  BACKUP="$TARGET/Poly8_previous_images_$(date +%Y%m%d_%H%M%S)"
  mkdir -p "$BACKUP"
  shopt -s nullglob
  OLD_IMAGES=("$TARGET"/LXR*.img)
  if (( ${#OLD_IMAGES[@]} > 0 )); then
    mv "${OLD_IMAGES[@]}" "$BACKUP/"
  fi
  cp "$IMG" "$TARGET/LXR_V2_V1.7.img"
  cmp -s "$IMG" "$TARGET/LXR_V2_V1.7.img" || fail 'Copied image does not match the build output.'
  say "Copied and verified: $TARGET/LXR_V2_V1.7.img"
  say "Previous LXR images, if any, were backed up in: $BACKUP"
else
  say 'No SD card path entered. The verified image remains in build/release/.'
fi

say 'Done. Safely eject the SD card before removing it.'
read -r -p 'Press Enter to close...' _
