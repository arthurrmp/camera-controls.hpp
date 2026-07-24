# macOS example app

The macOS version of the [iOS example](../ios-app): the same `Viewer.mm`
and the same assets, with an AppKit view that forwards mouse events to
the library's mouse layer. Left-drag orbits, right-drag trucks,
middle-drag dollies, and the scroll wheel and the trackpad pinch dolly
at the cursor.

## Build

Requirements: Xcode, [XcodeGen](https://github.com/yonaskolb/XcodeGen)
(`brew install xcodegen`).

```sh
./setup.sh              # Filament v1.74.0 for macOS + the example assets
xcodegen                # generates CameraControlsExampleMac.xcodeproj
open CameraControlsExampleMac.xcodeproj
```

The asset sources and licenses are listed in the
[iOS example README](../ios-app/README.md).
