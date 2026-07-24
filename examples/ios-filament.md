# iOS and Filament integration

This document shows an integration of `camera-controls.hpp` with UIKit
and Google Filament. The same structure applies to Android
(`MotionEvent` and `Choreographer`) or another windowing layer. The
[`ios-app`](ios-app) example is a complete, buildable version of this.

## Per-frame update (ObjC++ / C++)

```cpp
#include "camera_controls.hpp"

camctl::CameraControls controls;

// After loading a model, frame it:
controls.minDistance = 0.8;
controls.maxDistance = 80.0;
controls.fitToSphere(modelCenter, modelRadius, false, fovYRadians, aspect);

// On resize (sizes in points, the same units as the touch positions):
controls.setViewport(widthPts, heightPts, std::tan(fovYRadians * 0.5));

// Every display-link tick:
controls.update(dt);
const camctl::Vec3 eye = controls.getPosition(false);
const camctl::Vec3 target = controls.getTarget(false);

const auto b = camctl::CameraControls::lookAt(eye, target);
// Do not use filament's camera->lookAt(). It changes the up vector near
// the poles, which rotates the view for one frame. Set the model matrix:
camera->setModelMatrix(filament::math::mat4{
    filament::math::double4{b.x.x, b.x.y, b.x.z, 0},
    filament::math::double4{b.y.x, b.y.y, b.y.z, 0},
    filament::math::double4{b.z.x, b.z.y, b.z.z, 0},
    filament::math::double4{eye.x, eye.y, eye.z, 1}});
```

Drive it at display refresh with a `CADisplayLink`
(`preferredFrameRateRange` 80...120 for ProMotion). Pause the link when
the app enters the background; Metal work is not allowed there.

## Touches (Swift)

The library decides the gesture, so the view only forwards raw touches.
One finger rotates, two fingers dolly and truck, a finger can join or
leave mid-gesture, and a double-tap-drag zooms on the tap point. The
view needs `isMultipleTouchEnabled = true`.

```swift
override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
    for touch in touches {
        let p = touch.location(in: self) // points
        viewer.touchBegan(id(touch), x: p.x, y: p.y, time: touch.timestamp)
    }
}
// touchesMoved -> touchMoved(id, x, y)
// touchesEnded -> touchEnded(id, time)
// touchesCancelled -> touchCancelled(id)

private func id(_ touch: UITouch) -> Int64 {
    Int64(Int(bitPattern: Unmanaged.passUnretained(touch).toOpaque()))
}
```

Do not use `UIPanGestureRecognizer` and `UIPinchGestureRecognizer` for
this: their state machines end with the touch sequence, so a finger
cannot join a drag or keep rotating after a pinch.
