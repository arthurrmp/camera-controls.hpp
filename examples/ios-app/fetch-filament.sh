#!/bin/bash
# Downloads the Filament iOS release and puts the headers and the static
# libraries that this example links against into vendor/filament (~45MB,
# not committed). Run once, or run again to change FILAMENT_VERSION.
set -euo pipefail

FILAMENT_VERSION="v1.74.0"
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR_DIR="$APP_DIR/vendor/filament"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

LIBS=(
  backend basis_transcoder camutils dracodec filabridge filaflat filament
  geometry gltfio_core ibl image ktxreader meshoptimizer shaders smol-v stb
  uberarchive uberzlib utils zstd abseil perfetto
)

echo "Downloading filament-$FILAMENT_VERSION-ios.tgz..."
curl -sL -o "$TMP_DIR/filament-ios.tgz" \
  "https://github.com/google/filament/releases/download/$FILAMENT_VERSION/filament-$FILAMENT_VERSION-ios.tgz"
tar xzf "$TMP_DIR/filament-ios.tgz" -C "$TMP_DIR"

rm -rf "$VENDOR_DIR"
mkdir -p "$VENDOR_DIR/lib"
cp -R "$TMP_DIR/filament/include" "$VENDOR_DIR/"
for lib in "${LIBS[@]}"; do
  cp -R "$TMP_DIR/filament/lib/lib${lib}.xcframework" "$VENDOR_DIR/lib/"
done

echo "Vendored Filament $FILAMENT_VERSION into $VENDOR_DIR"
