# camera-controls.hpp

C++17 port of the touch controls from
[yomotsu/camera-controls](https://github.com/yomotsu/camera-controls), the
damped orbit-control library for three.js.

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

// Send input from your gesture handlers. Drag deltas are (last - current).
controls.rotatePixels(dx, dy, viewportHeightPx);           // one-finger drag
controls.dollyPinchDelta(prevPinchDist - pinchDist);       // pinch, in points
controls.truckPixels(midDx, midDy, viewportHeightPx, tanHalfFov); // two-finger pan
controls.endRotate();                                      // on gesture end

// Update once per frame. Apply the result to your camera.
controls.update(dt);
const camctl::Vec3 eye = controls.getPosition(false);
const camctl::Vec3 target = controls.getTarget(false);
const auto basis = camctl::CameraControls::lookAt(eye, target);
// The camera model matrix columns are (basis.x, 0), (basis.y, 0),
// (basis.z, 0), (eye, 1).
```

To build and run the example:

```sh
cd examples && c++ -std=c++17 -I../include minimal.cpp -o minimal && ./minimal
```

The example does not need a renderer. It prints the camera state for a drag,
a release, and a pinch.

See [`examples/ios-filament.md`](examples/ios-filament.md) for an
integration with UIKit gestures and Filament, and
[`examples/ios-app`](examples/ios-app) for a complete SwiftUI application
that renders a glTF model with Filament and all three gestures.

To use the library as a dependency instead of copying the header: the repo
is a Swift package (`Package.swift`) and a CMake project (an `INTERFACE`
target named `camera_controls::camera_controls`).

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

The original library reads DOM pointer and wheel events itself. A native
application sends the same deltas through the input layer instead:

- `rotatePixels` and `truckPixels` for drags. Mouse and touch use the same
  formula in the original, so these serve both.
- `dollyPinchDelta` for a pinch, `dollyWheelDelta` for a mouse wheel.
- `endRotate` / `endDolly` / `endTruck` when a gesture ends.

## Input units

| Input | Units | Reason |
| --- | --- | --- |
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
- Mouse wheel dolly, including zoom at the cursor
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

- `dollyDeltaAnchored`: one-finger zoom that keeps the point under a screen
  anchor in place. This is the double-press-and-drag gesture from map
  applications.
- `CameraControls::lookAt()`: a camera basis from eye and target, with the
  three.js `Matrix4.lookAt` construction. Use it if the lookAt function of
  your engine is not stable when the view direction is almost vertical.
  The doc comment in the header has the details.

## Features that are not ported

- Keyboard input and the `ACTION` mapping
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
