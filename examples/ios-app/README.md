# iOS example app

A SwiftUI app that renders a glTF cube with
[Filament](https://github.com/google/filament) and moves the camera with
`camera-controls.hpp`. Gestures: one-finger orbit, pinch zoom, and
tap-then-drag one-finger zoom that pivots on the tap point.

The app shows the full integration surface:

- `Viewer.mm` (Objective-C++): Filament setup, gltfio model loading, and the
  per-frame `update()` + `lookAt()` + `setModelMatrix()` step
- `ViewerView.swift`: `CADisplayLink` at up to 120Hz and the gesture
  handlers, with the pixel and point unit conventions in comments
- `Viewer.h`: the small Objective-C interface between the two

## Build

Requirements: Xcode, [XcodeGen](https://github.com/yonaskolb/XcodeGen)
(`brew install xcodegen`).

```sh
./fetch-filament.sh     # downloads Filament v1.74.0 (~45MB into vendor/)
xcodegen                # generates CameraControlsExample.xcodeproj
open CameraControlsExample.xcodeproj
```

Then build and run on a simulator or a device. On a ProMotion device the
display link runs at 120Hz.
