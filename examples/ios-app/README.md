# iOS example app

A SwiftUI app that renders the Khronos Avocado glTF model with
[Filament](https://github.com/google/filament) and moves the camera with
`camera-controls.hpp`. Gestures: one-finger orbit, pinch zoom, and
double-tap-then-drag one-finger zoom that pivots on the tap point. A
Liquid Glass overlay shows the frame rate and a short gesture guide
(this is why the deployment target is iOS 26).

The app shows the full integration surface:

- `Viewer.mm` (Objective-C++): Filament setup (image-based light, key
  light, MSAA), gltfio model loading, and the per-frame `update()` +
  `lookAt()` + `setModelMatrix()` step; the model is framed with
  `fitToSphere`
- `ViewerView.swift`: `CADisplayLink` at up to 120Hz and the gesture
  handlers, with the pixel and point unit conventions in comments
- `App.swift`: the SwiftUI overlay (stats badge and gesture guide)
- `Viewer.h`: the small Objective-C interface between Swift and the C++

## Build

Requirements: Xcode, [XcodeGen](https://github.com/yonaskolb/XcodeGen)
(`brew install xcodegen`).

```sh
./setup.sh              # Filament v1.74.0 (~45MB) + the example assets
xcodegen                # generates CameraControlsExample.xcodeproj
open CameraControlsExample.xcodeproj
```

Then build and run on a simulator or a device. On a ProMotion device the
display link runs at 120Hz. To run on a device, set `DEVELOPMENT_TEAM` in
`project.yml` first.

## Assets

`setup.sh` downloads the assets; they are not committed:

- `Avocado.glb` from the
  [Khronos glTF sample assets](https://github.com/KhronosGroup/glTF-Sample-Assets)
  (CC0, public domain)
- `default_env_ibl.ktx` from the
  [Filament](https://github.com/google/filament) repository (Apache 2.0)

Without them the app falls back to the committed `cube.glb` and plain
directional light.
