# iOS and Filament integration

This document shows an integration of `camera-controls.hpp` with UIKit
gestures and Google Filament. The same structure applies to Android
(`GestureDetector`, `ScaleGestureDetector`, and `Choreographer`) or another
windowing layer.

## Per-frame update (ObjC++ / C++)

```cpp
#include "camera_controls.hpp"

camctl::CameraControls controls;

// After loading a model, frame it:
controls.minDistance = 0.8;
controls.maxDistance = 80.0;
controls.setLookAt(initialEye.x, initialEye.y, initialEye.z,
                   modelCenter.x, modelCenter.y, modelCenter.z);

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

## Gestures (Swift)

Drive it at display refresh with a `CADisplayLink`
(`preferredFrameRateRange` 80...120 for ProMotion). All drag deltas are
`last - current`, matching the web library's pointer convention.

```swift
// One-finger orbit (deltas in physical px; viewport height physical too).
@objc func pan(_ g: UIPanGestureRecognizer) {
    let p = pixelPoint(g.location(in: self))
    switch g.state {
    case .began:   lastGrab = p
    case .changed: controls.rotatePixels(lastGrab.x - p.x, lastGrab.y - p.y,
                                         viewportHeightPx); lastGrab = p
    default:       controls.endRotate()
    }
}

// Pinch: dolly from the touch distance delta in points, truck from the
// midpoint in physical pixels. The truck moves the zoom toward the pinch.
// Points are necessary for the dolly: physical pixels zoom too fast.
@objc func pinch(_ g: UIPinchGestureRecognizer) {
    guard g.numberOfTouches >= 2 else { if g.state != .changed { controls.endDolly(); controls.endTruck() }; return }
    let a = g.location(ofTouch: 0, in: self), b = g.location(ofTouch: 1, in: self)
    let dist = hypot(a.x - b.x, a.y - b.y)                    // points
    let mid = CGPoint(x: (a.x + b.x) / 2 * screenScale,       // physical px
                      y: (a.y + b.y) / 2 * screenScale)
    switch g.state {
    case .began:
        lastDist = dist; lastMid = mid
    case .changed:
        controls.dollyPinchDelta(lastDist - dist)
        controls.truckPixels(lastMid.x - mid.x, lastMid.y - mid.y,
                             viewportHeightPx, tanHalfFov)
        lastDist = dist; lastMid = mid
    default:
        controls.endDolly(); controls.endTruck()
    }
}
```

## One-finger zoom (map style)

A tap starts a 0.35 s window. A drag that starts near the tap becomes an
anchored dolly: down zooms in, up zooms out, and the point under the tap
stays in place.

```swift
// In the pan handler's .began, when primed:
zoomAnchorPx = tapLocation * screenScale
// In .changed while zooming (delta in points, anchor in physical px):
controls.dollyDeltaAnchored(lastY - p.y, zoomAnchorPx.x, zoomAnchorPx.y,
                            viewportWidthPx, viewportHeightPx, tanHalfFov)
```
