#!/bin/bash
# Downloads the Filament macOS release into vendor/filament (not
# committed), and the shared example assets into ../ios-app/Resources if
# they are not there yet.
set -euo pipefail

FILAMENT_VERSION="v1.74.0"
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR_DIR="$APP_DIR/vendor/filament"
RES_DIR="$APP_DIR/../ios-app/Resources"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

LIBS=(
  backend basis_transcoder bluegl bluevk camutils dracodec filabridge
  filaflat filament geometry gltfio_core ibl image ktxreader meshoptimizer
  shaders smol-v stb uberarchive uberzlib utils zstd abseil perfetto
)

echo "Downloading filament-$FILAMENT_VERSION-mac.tgz..."
curl -sL -o "$TMP_DIR/filament-mac.tgz" \
  "https://github.com/google/filament/releases/download/$FILAMENT_VERSION/filament-$FILAMENT_VERSION-mac.tgz"
tar xzf "$TMP_DIR/filament-mac.tgz" -C "$TMP_DIR"

# The macOS release of v1.74.0 ships arm64 only.
rm -rf "$VENDOR_DIR"
mkdir -p "$VENDOR_DIR/lib/arm64"
cp -R "$TMP_DIR/filament/include" "$VENDOR_DIR/"
for lib in "${LIBS[@]}"; do
  cp "$TMP_DIR/filament/lib/arm64/lib${lib}.a" "$VENDOR_DIR/lib/arm64/"
done
echo "Vendored Filament $FILAMENT_VERSION into $VENDOR_DIR"

if [ ! -f "$RES_DIR/Avocado.glb" ]; then
  echo "Downloading example assets..."
  curl -sL -o "$RES_DIR/Avocado.glb" \
    "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/Avocado/glTF-Binary/Avocado.glb"
  curl -sL -o "$RES_DIR/default_env_ibl.ktx" \
    "https://raw.githubusercontent.com/google/filament/$FILAMENT_VERSION/docs/web/assets/helmet/default_env/default_env_ibl.ktx"
fi
echo "Done."
