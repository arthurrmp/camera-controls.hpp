# Metal example app

A macOS app that renders a cube with plain Metal, no engine, and moves
the camera with `camera-controls.hpp`. It has no dependencies to
download: the whole example is the renderer, the shader, and the view.

`Renderer.mm` shows the part an engine normally hides: the view matrix
built from the `lookAt` basis. The basis vectors are the columns of the
camera model matrix, so the view matrix (its inverse) has them as rows,
with `-dot(axis, eye)` as the translation.

Controls: left-drag orbits, right-drag pans, middle-drag zooms, and the
scroll wheel and the trackpad pinch zoom at the cursor.

## Build

Requirements: Xcode, [XcodeGen](https://github.com/yonaskolb/XcodeGen)
(`brew install xcodegen`).

```sh
xcodegen
open CameraControlsExampleMetal.xcodeproj
```
