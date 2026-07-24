# camera-controls.hpp

C++17 port of the camera controls from
[yomotsu/camera-controls](https://github.com/yomotsu/camera-controls), the
damped orbit-control library for three.js. Touch and mouse input, the
damped orbit, dolly, and truck movement, and the fitting functions.

The library is one header file with no dependencies. You can use it with
[Filament](https://github.com/google/filament), Metal, Vulkan, OpenGL, or
another renderer that has a look-at camera.

The port follows the camera-controls v3.1.2 API and copies its constants
and its damping function. A native application that uses this library has
the same camera movement as a web application that uses the original
library.

## Usage

```cpp
#include "camera_controls.hpp"

camctl::CameraControls controls;
controls.setLookAt(/*position*/ 0, 3, 5, /*target*/ 0, 1, 0);
controls.setViewport(widthPts, heightPts, tanHalfFov);

// Forward every touch. The controls decide the gesture: one finger
// rotates, two fingers dolly and truck, and a double-tap-drag zooms on
// the tap point. Positions are in density-independent pixels.
controls.touchBegan(touchId, x, y, timeSeconds);
controls.touchMoved(touchId, x, y);
controls.touchEnded(touchId, timeSeconds);

// Update once per frame. Apply the result to your camera.
controls.update(dt);
const camctl::Vec3 eye = controls.getPosition(false);
const camctl::Vec3 target = controls.getTarget(false);
const auto basis = camctl::CameraControls::lookAt(eye, target);
// The camera model matrix columns are (basis.x, 0), (basis.y, 0),
// (basis.z, 0), (eye, 1).
```

To use the library as a dependency instead of copying the header: the repo
is a Swift package (`Package.swift`) and a CMake project (an `INTERFACE`
target named `camera_controls::camera_controls`).

## Examples

- [`examples/ios-app`](examples/ios-app): SwiftUI + Filament, the touch
  layer, a glTF model with image-based lighting
- [`examples/mac-app`](examples/mac-app): the macOS version, with the
  mouse layer
- [`examples/metal-app`](examples/metal-app): plain Metal, no engine and
  nothing to download; shows the view matrix built from the lookAt basis
- [`examples/vulkan-app`](examples/vulkan-app): plain Vulkan with GLFW,
  cross-platform; on macOS it runs through MoltenVK

[`examples/ios-filament.md`](examples/ios-filament.md) documents the
UIKit + Filament integration in prose.

[`examples/minimal.cpp`](examples/minimal.cpp) is the renderer-free
check: it drives the touch layer and fitToBox and prints the camera
state. CI runs it on macOS and Linux.

```sh
cd examples && c++ -std=c++17 -I../include minimal.cpp -o minimal && ./minimal
```

## API

The names, the defaults, and the semantics come from camera-controls
v3.1.2. Differences that C++ makes necessary:

- Property get/set pairs become functions: `azimuthAngle()`,
  `polarAngle()`, `distance()`. The setters map to `rotateTo()` and
  `dollyTo()`.
- Methods return `void`, not a `Promise`.
- `getPosition()`, `getTarget()`, and `getSpherical()` return values
  instead of copying into an out object. As in the original, they return
  the transition end values; pass `false` for the current damped values.
- `setBoundary()` and `fitToBox()` take two `Vec3` corners instead of a
  `THREE.Box3`.
- The library has no camera object, so `fitToBox()`, `fitToSphere()`, and
  the `getDistanceToFit*()` functions take the vertical field of view in
  radians and the viewport aspect ratio as arguments.

The original library reads DOM pointer and wheel events and decides the
gesture itself. The input layers do the same natively; call
`setViewport()` first, then:

- Touch: forward every touch to `touchBegan` / `touchMoved` /
  `touchEnded` / `touchCancelled`. A finger can join or leave a gesture
  mid-drag, as on the web.
- Mouse: forward `mouseDown` / `mouseMoved` / `mouseUp`, and the wheel to
  `mouseWheel(deltaY, x, y)` for zoom at the cursor, or to
  `dollyWheelDelta` for a plain dolly. The `mouseButtons` mapping has the
  original defaults: left rotates, middle dollies, right trucks;
  `dollyDragInverted` is ported too.

Under them, the delta functions (`rotatePixels`, `dollyPinchDelta`,
`truckPixels`, `endRotate` / `endDolly` / `endTruck`) serve custom input.

## Input units

| Input | Units | Reason |
| --- | --- | --- |
| touch and mouse layer positions, and `setViewport` | density-independent pixels (iOS points, Android dp, css pixels) | the same units the web library gets from the DOM |
| `rotatePixels`, `truckPixels` deltas | any, the same as `viewportHeight` | only the ratio has an effect |
| `dollyPinchDelta`, `dollyDeltaAnchored` delta | density-independent pixels (iOS points, css pixels) | the dolly curve `0.95^(px/8)` uses absolute pixels |
| `dollyWheelDelta`, `dollyWheelDeltaAnchored` delta | pixel-mode wheel units, positive = down | the curve is `0.95^(units/10)`, the macOS feel; divide by 3 for the other systems' feel |
| all drag deltas | `last - current` | the pointer convention of camera-controls |
| anchor and viewport in `dollyDeltaAnchored` | the same as each other | normalized internally |

## Ported features

- The SmoothDamp damping function, with the overshoot correction
- `smoothTime = 0.25` after release, `draggingSmoothTime = 0.125` during a
  gesture, applied per axis, with the `1e-5` snap threshold
- Touch orbit: `2π · px / viewportHeight` on both axes
- Touch pinch dolly: factor `1/8`, curve `0.95^(-Δ · dollySpeed)`
- Touch truck (`truckSpeed = 2`), the part of `TOUCH_DOLLY_TRUCK` that moves
  the zoom toward the pinch midpoint
- Mouse input: the `mouseButtons` mapping with its defaults,
  `dollyDragInverted`, and wheel dolly including zoom at the cursor
  (`dollyWheelDeltaAnchored`)
- The methods `rotate`, `rotateTo`, `rotateAzimuthTo`, `rotatePolarTo`,
  `dolly`, `dollyTo`, `truck`, `moveTo`, `setTarget`, `setPosition`,
  `setLookAt`, `setBoundary`, `getPosition`, `getTarget`, `getSpherical`,
  and `update`, with the `enableTransition` argument
- `fitToBox` (with `cover` and the padding options) and `fitToSphere`,
  and the `getDistanceToFitBox` / `getDistanceToFitSphere` helpers
- The limits `minDistance`, `maxDistance`, `minPolarAngle`,
  `maxPolarAngle`, `minAzimuthAngle`, `maxAzimuthAngle`, and a damped
  target with a panning boundary

## Added features

These are not in the original library:

- The anchored one-finger zoom: a drag that keeps the point under a
  screen anchor in place. In the touch layer this is the
  double-tap-and-drag gesture from map applications; the low-level entry
  is `dollyDeltaAnchored`.
- `CameraControls::lookAt()`: the original ends its update with
  `camera.lookAt(target)` and lets the three.js camera build the view
  basis. This library has no camera object, so it provides that step as a
  static function, with the three.js `Matrix4.lookAt` construction. Use
  it if the lookAt function of your engine is not stable when the view
  direction is almost vertical; the doc comment in the header has the
  details.

## Features that are not ported

- Keyboard input, and remapping of the touch gestures (the touch layer
  uses the default touch mapping)
- `lerp` and `lerpLookAt`
- `infinityDolly` and collision (`colliderMeshes`)
- Focal offset, orthographic zoom, screen-space panning,
  `verticalDragToForward`, `forward`, `elevate`, `lookInDirectionOf`
- `boundaryFriction` and `boundaryEnclosesCamera` (the boundary clamps
  without friction)
- `saveState`, `toJSON`, `fromJSON`, and events

## License

MIT. This is a port of
[camera-controls](https://github.com/yomotsu/camera-controls),
© 2017 [@yomotsu](https://github.com/yomotsu), MIT. See [LICENSE](LICENSE).
