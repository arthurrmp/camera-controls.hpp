// camera-controls.hpp
//
// C++17 port of the touch controls from yomotsu/camera-controls
// (https://github.com/yomotsu/camera-controls, MIT).
//
// https://github.com/arthurrmp/camera-controls.hpp
//
// The library is one header file. It has no dependencies. Send input
// deltas to it, call update(dt) each frame, and apply getPosition(false)
// and getTarget(false) to your camera. Use lookAt() to get a camera basis
// that is stable at the poles.
//
// The API follows camera-controls v3.1.2. Differences that C++ makes
// necessary:
//   - property get/set pairs become functions: azimuthAngle(),
//     polarAngle(), distance(); the setters map to rotateTo() and
//     dollyTo()
//   - methods return void, not a Promise
//   - getPosition(), getTarget(), and getSpherical() return values
//     instead of copying into an out object
//
// Input conventions (the same as in the web library):
//   - drag deltas are (last - current), in pixels
//   - rotation divides both axes by the viewport height
//   - dolly deltas must be in density-independent pixels (iOS points or
//     css pixels). Rotation and truck can use physical pixels if the
//     viewport height is also physical. Only the ratio has an effect.

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace camctl {

struct Vec3 {
    double x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
};

inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 normalize(const Vec3 &v) {
    const double l = std::sqrt(dot(v, v));
    return l > 0 ? v * (1.0 / l) : v;
}

/// Minimal quaternion. fitToBox() uses it to orient the box in view space.
struct Quat {
    double x = 0, y = 0, z = 0, w = 1;

    static Quat fromUnitVectors(const Vec3 &from, const Vec3 &to) {
        const double r = dot(from, to) + 1.0;
        Quat q;
        if (r < 1e-12) {
            // The vectors point in opposite directions.
            if (std::abs(from.x) > std::abs(from.z)) q = {-from.y, from.x, 0.0, 0.0};
            else q = {0.0, -from.z, from.y, 0.0};
        } else {
            const Vec3 c = cross(from, to);
            q = {c.x, c.y, c.z, r};
        }
        const double l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        return {q.x / l, q.y / l, q.z / l, q.w / l};
    }

    static Quat fromAxisAngle(const Vec3 &axis, double angle) {
        const double s = std::sin(angle * 0.5);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5)};
    }

    Quat operator*(const Quat &b) const {
        return {w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w,
                w * b.w - x * b.x - y * b.y - z * b.z};
    }

    Quat conjugate() const { return {-x, -y, -z, w}; }

    Vec3 rotate(const Vec3 &v) const {
        const Vec3 qv{x, y, z};
        const Vec3 t = cross(qv, v) * 2.0;
        return v + t * w + cross(qv, t);
    }
};

struct FitToOptions {
    bool cover = false;
    double paddingLeft = 0;
    double paddingRight = 0;
    double paddingBottom = 0;
    double paddingTop = 0;
};

class CameraControls {
public:
    static constexpr double kPi = 3.141592653589793;
    static constexpr double kHalfPi = 1.5707963267948966;
    static constexpr double kInfinity = std::numeric_limits<double>::infinity();

    // Properties of the original library, with the v3.1.2 defaults.
    double smoothTime = 0.25;
    double draggingSmoothTime = 0.125;
    double azimuthRotateSpeed = 1.0;
    double polarRotateSpeed = 1.0;
    double dollySpeed = 1.0;
    double truckSpeed = 2.0;
    double minDistance = std::numeric_limits<double>::epsilon();
    double maxDistance = kInfinity;
    double minPolarAngle = 0.0;
    double maxPolarAngle = kPi;
    double minAzimuthAngle = -kInfinity;
    double maxAzimuthAngle = kInfinity;

    // Property getters. The property setters of the original library map
    // to rotateTo() and dollyTo().
    double azimuthAngle() const { return theta_; }
    double polarAngle() const { return phi_; }
    double distance() const { return radius_; }

    struct Spherical {
        double radius, phi, theta;
    };

    Spherical getSpherical(bool receiveEndValue = true) const {
        return receiveEndValue ? Spherical{radiusEnd_, phiEnd_, thetaEnd_}
                               : Spherical{radius_, phi_, theta_};
    }

    Vec3 getTarget(bool receiveEndValue = true) const {
        return receiveEndValue ? targetEnd_ : target_;
    }

    Vec3 getPosition(bool receiveEndValue = true) const {
        const Spherical s = getSpherical(receiveEndValue);
        const Vec3 t = getTarget(receiveEndValue);
        const double sinPhi = std::sin(s.phi);
        return {t.x + s.radius * sinPhi * std::sin(s.theta),
                t.y + s.radius * std::cos(s.phi),
                t.z + s.radius * sinPhi * std::cos(s.theta)};
    }

    // Methods of the original library.

    void rotate(double azimuthAngle, double polarAngle, bool enableTransition = false) {
        rotateTo(thetaEnd_ + azimuthAngle, phiEnd_ + polarAngle, enableTransition);
    }

    void rotateAzimuthTo(double azimuthAngle, bool enableTransition = false) {
        rotateTo(azimuthAngle, phiEnd_, enableTransition);
    }

    void rotatePolarTo(double polarAngle, bool enableTransition = false) {
        rotateTo(thetaEnd_, polarAngle, enableTransition);
    }

    void rotateTo(double azimuthAngle, double polarAngle, bool enableTransition = false) {
        userRotating_ = false;
        thetaEnd_ = std::clamp(azimuthAngle, minAzimuthAngle, maxAzimuthAngle);
        phiEnd_ = std::clamp(polarAngle, minPolarAngle, maxPolarAngle);
        phiEnd_ = std::clamp(phiEnd_, kMakeSafeEps, kPi - kMakeSafeEps);
        if (!enableTransition) {
            theta_ = thetaEnd_;
            phi_ = phiEnd_;
        }
    }

    void dolly(double distance, bool enableTransition = false) {
        dollyTo(radiusEnd_ - distance, enableTransition);
    }

    void dollyTo(double distance, bool enableTransition = false) {
        userDollying_ = false;
        radiusEnd_ = std::clamp(distance, minDistance, maxDistance);
        if (!enableTransition) radius_ = radiusEnd_;
    }

    void truck(double x, double y, bool enableTransition = false) {
        Vec3 xAxis, yAxis, zAxis;
        basisAxes(xAxis, yAxis, zAxis);
        const Vec3 to = targetEnd_ + xAxis * x + yAxis * (-y);
        moveTo(to.x, to.y, to.z, enableTransition);
    }

    void moveTo(double x, double y, double z, bool enableTransition = false) {
        userTrucking_ = false;
        targetEnd_ = {x, y, z};
        clampTarget();
        if (!enableTransition) target_ = targetEnd_;
    }

    void setTarget(double targetX, double targetY, double targetZ,
                   bool enableTransition = false) {
        const Vec3 pos = getPosition(true);
        setLookAt(pos.x, pos.y, pos.z, targetX, targetY, targetZ, enableTransition);
        phiEnd_ = std::clamp(phiEnd_, minPolarAngle, maxPolarAngle);
    }

    void setPosition(double positionX, double positionY, double positionZ,
                     bool enableTransition = false) {
        setLookAt(positionX, positionY, positionZ,
                  targetEnd_.x, targetEnd_.y, targetEnd_.z, enableTransition);
    }

    void setLookAt(double positionX, double positionY, double positionZ,
                   double targetX, double targetY, double targetZ,
                   bool enableTransition = false) {
        userRotating_ = userDollying_ = userTrucking_ = false;
        targetEnd_ = {targetX, targetY, targetZ};
        const Vec3 rel = Vec3{positionX, positionY, positionZ} - targetEnd_;
        const double r = std::sqrt(dot(rel, rel));
        radiusEnd_ = r;
        thetaEnd_ = std::atan2(rel.x, rel.z);
        phiEnd_ = r == 0.0 ? 0.0 : std::acos(std::clamp(rel.y / r, -1.0, 1.0));
        if (!enableTransition) {
            target_ = targetEnd_;
            theta_ = thetaEnd_;
            phi_ = phiEnd_;
            radius_ = radiusEnd_;
        }
    }

    void setBoundary(const Vec3 &min, const Vec3 &max) {
        boundaryMin_ = min;
        boundaryMax_ = max;
        clampTarget();
    }

    void setBoundary() {
        boundaryMin_ = {-kInfinity, -kInfinity, -kInfinity};
        boundaryMax_ = {kInfinity, kInfinity, kInfinity};
    }

    /// Fit the view to an axis-aligned box. The camera first rotates to the
    /// nearest 90-degree view, as in the original library. The library has
    /// no camera object, so the caller passes the vertical field of view in
    /// radians and the viewport aspect ratio.
    void fitToBox(const Vec3 &boxMin, const Vec3 &boxMax, bool enableTransition,
                  double fovY, double aspect, const FitToOptions &options = {}) {
        const double theta = roundToStep(thetaEnd_, kHalfPi);
        const double phi = roundToStep(phiEnd_, kHalfPi);
        rotateTo(theta, phi, enableTransition);

        const Vec3 normal = {std::sin(phiEnd_) * std::sin(thetaEnd_),
                             std::cos(phiEnd_),
                             std::sin(phiEnd_) * std::cos(thetaEnd_)};
        Quat rotation = Quat::fromUnitVectors(normal, {0, 0, 1});
        const bool viewFromPolar = std::abs(std::abs(normal.y) - 1.0) < kEpsilon;
        const Quat yaw = Quat::fromAxisAngle({0, 1, 0}, theta);
        if (viewFromPolar) rotation = rotation * yaw;

        // The box corners in view space give the oriented bounding box.
        Vec3 bbMin{kInfinity, kInfinity, kInfinity};
        Vec3 bbMax{-kInfinity, -kInfinity, -kInfinity};
        for (int i = 0; i < 8; i++) {
            const Vec3 corner{(i & 1) ? boxMax.x : boxMin.x,
                              (i & 2) ? boxMax.y : boxMin.y,
                              (i & 4) ? boxMax.z : boxMin.z};
            const Vec3 p = rotation.rotate(corner);
            bbMin = {std::min(bbMin.x, p.x), std::min(bbMin.y, p.y), std::min(bbMin.z, p.z)};
            bbMax = {std::max(bbMax.x, p.x), std::max(bbMax.y, p.y), std::max(bbMax.z, p.z)};
        }
        bbMin.x -= options.paddingLeft;
        bbMin.y -= options.paddingBottom;
        bbMax.x += options.paddingRight;
        bbMax.y += options.paddingTop;

        Quat back = Quat::fromUnitVectors({0, 0, 1}, normal);
        if (viewFromPolar) back = yaw.conjugate() * back;

        const Vec3 size = bbMax - bbMin;
        const Vec3 center = back.rotate((bbMin + bbMax) * 0.5);
        const double distance =
            getDistanceToFitBox(size.x, size.y, size.z, fovY, aspect, options.cover);
        moveTo(center.x, center.y, center.z, enableTransition);
        dollyTo(distance, enableTransition);
    }

    /// Fit the view to a sphere.
    void fitToSphere(const Vec3 &center, double radius, bool enableTransition,
                     double fovY, double aspect) {
        moveTo(center.x, center.y, center.z, enableTransition);
        dollyTo(getDistanceToFitSphere(radius, fovY, aspect), enableTransition);
    }

    double getDistanceToFitBox(double width, double height, double depth,
                               double fovY, double aspect, bool cover = false) const {
        const double boundingRectAspect = width / height;
        const double heightToFit =
            (cover ? boundingRectAspect > aspect : boundingRectAspect < aspect)
                ? height
                : width / aspect;
        return heightToFit * 0.5 / std::tan(fovY * 0.5) + depth * 0.5;
    }

    double getDistanceToFitSphere(double radius, double fovY, double aspect) const {
        const double hFov = std::atan(std::tan(fovY * 0.5) * aspect) * 2.0;
        const double fov = aspect > 1.0 ? fovY : hFov;
        return radius / std::sin(fov * 0.5);
    }

    /// Advance the damped state. Returns true if the state changed.
    bool update(double delta) {
        const double dt = std::max(delta, 1e-6);
        const double prevTheta = theta_, prevPhi = phi_, prevRadius = radius_;
        const Vec3 prevTarget = target_;

        stepAxis(theta_, thetaEnd_, thetaVel_, userRotating_, dt);
        stepAxis(phi_, phiEnd_, phiVel_, userRotating_, dt);
        stepAxis(radius_, radiusEnd_, radiusVel_, userDollying_, dt);
        // Post-damp clamps (Spherical.makeSafe in three.js): the overshoot
        // correction can push the smoothed value past the pole for a frame,
        // which flips sin(phi) and mirrors the camera.
        phi_ = std::clamp(phi_, std::max(minPolarAngle, kMakeSafeEps),
                          std::min(maxPolarAngle, kPi - kMakeSafeEps));
        radius_ = std::clamp(radius_, minDistance, maxDistance);

        const Vec3 dTarget = targetEnd_ - target_;
        if (std::abs(dTarget.x) < kEpsilon && std::abs(dTarget.y) < kEpsilon &&
            std::abs(dTarget.z) < kEpsilon) {
            targetVel_ = {};
            target_ = targetEnd_;
        } else {
            const double st = userTrucking_ ? draggingSmoothTime : smoothTime;
            target_.x = smoothDamp(target_.x, targetEnd_.x, targetVel_.x, st, kMaxSpeed, dt);
            target_.y = smoothDamp(target_.y, targetEnd_.y, targetVel_.y, st, kMaxSpeed, dt);
            target_.z = smoothDamp(target_.z, targetEnd_.z, targetVel_.z, st, kMaxSpeed, dt);
        }

        return theta_ != prevTheta || phi_ != prevPhi || radius_ != prevRadius ||
               target_.x != prevTarget.x || target_.y != prevTarget.y ||
               target_.z != prevTarget.z;
    }

    // Touch layer. The original library reads pointer events and decides
    // the gesture itself; this layer is the same. Send every touch, and
    // the controls rotate with one finger, dolly and truck with two, and
    // change between the two without a jump when a finger joins or
    // leaves. A quick tap primes a short window; a touch that starts
    // inside the window becomes the anchored one-finger zoom (drag down
    // to dolly in, up to dolly out, around the point under the tap).
    //
    // Positions are in density-independent pixels (iOS points, Android
    // dp, css pixels). Call setViewport() first, in the same units.
    // Times are in seconds from any monotonic clock.

    void setViewport(double width, double height, double tanHalfFovValue) {
        viewportW_ = width;
        viewportH_ = height;
        tanHalfFov_ = tanHalfFovValue;
    }

    void touchBegan(long long touchId, double x, double y, double timeSeconds) {
        if (viewportH_ <= 0) return;
        if (touchCount_ < kMaxTouches && findTouch(touchId) < 0) {
            touches_[touchCount_++] = {touchId, x, y, x, y, timeSeconds};
        }
        syncTouchMode(timeSeconds);
    }

    void touchMoved(long long touchId, double x, double y) {
        const int i = findTouch(touchId);
        if (i < 0) return;
        touches_[i].x = x;
        touches_[i].y = y;
        switch (touchMode_) {
        case TouchMode::Rotate:
            if (i == 0) {
                rotatePixels(grabX_ - x, grabY_ - y, viewportH_);
                grabX_ = x;
                grabY_ = y;
            }
            break;
        case TouchMode::AnchoredZoom:
            if (i == 0) {
                dollyDeltaAnchored(zoomY_ - y, tapX_, tapY_,
                                   viewportW_, viewportH_, tanHalfFov_);
                zoomY_ = y;
            }
            break;
        case TouchMode::Pinch: {
            if (touchCount_ < 2) break;
            double distance, midX, midY;
            pinchStateNow(distance, midX, midY);
            if (pinchDistance_ > 0) {
                dollyPinchDelta(pinchDistance_ - distance);
                truckPixels(pinchMidX_ - midX, pinchMidY_ - midY,
                            viewportH_, tanHalfFov_);
            }
            pinchDistance_ = distance;
            pinchMidX_ = midX;
            pinchMidY_ = midY;
            break;
        }
        case TouchMode::Idle:
            break;
        }
    }

    void touchEnded(long long touchId, double timeSeconds) {
        const int i = findTouch(touchId);
        if (i < 0) return;
        const Touch &t = touches_[i];
        // A quick touch that did not move far is a tap; it primes the
        // anchored zoom window.
        if (touchCount_ == 1 && touchMode_ == TouchMode::Rotate &&
            timeSeconds - t.beganTime < kTapMaxSeconds &&
            std::abs(t.x - t.beganX) < kTapSlop &&
            std::abs(t.y - t.beganY) < kTapSlop) {
            tapTime_ = timeSeconds;
            tapX_ = t.beganX;
            tapY_ = t.beganY;
        }
        removeTouch(i);
        syncTouchMode(timeSeconds);
    }

    void touchCancelled(long long touchId) {
        const int i = findTouch(touchId);
        if (i < 0) return;
        removeTouch(i);
        syncTouchMode(-1e9);
    }

    // Mouse layer. Same idea as the touch layer: send the events, and the
    // button-to-action mapping decides the gesture. The defaults are the
    // v3.1.2 defaults: left rotates, middle dollies, right trucks. Use
    // dollyWheelDelta or dollyWheelDeltaAnchored for the wheel. Positions
    // are in the same units as setViewport().

    enum class MouseAction { None, Rotate, Dolly, Truck };

    struct MouseButtons {
        MouseAction left = MouseAction::Rotate;
        MouseAction middle = MouseAction::Dolly;
        MouseAction right = MouseAction::Truck;
    };

    MouseButtons mouseButtons;
    bool dollyDragInverted = false;

    /// button: 0 = left, 1 = middle, 2 = right.
    void mouseDown(int button, double x, double y) {
        if (viewportH_ <= 0) return;
        mouseAction_ = button == 0   ? mouseButtons.left
                       : button == 1 ? mouseButtons.middle
                       : button == 2 ? mouseButtons.right
                                     : MouseAction::None;
        mouseX_ = x;
        mouseY_ = y;
    }

    void mouseMoved(double x, double y) {
        const double dx = mouseX_ - x;
        const double dy = mouseY_ - y;
        mouseX_ = x;
        mouseY_ = y;
        switch (mouseAction_) {
        case MouseAction::Rotate:
            rotatePixels(dx, dy, viewportH_);
            break;
        case MouseAction::Dolly:
            dollyPinchDelta((dollyDragInverted ? -1.0 : 1.0) * dy);
            break;
        case MouseAction::Truck:
            truckPixels(dx, dy, viewportH_, tanHalfFov_);
            break;
        case MouseAction::None:
            break;
        }
    }

    void mouseUp() {
        switch (mouseAction_) {
        case MouseAction::Rotate:
            endRotate();
            break;
        case MouseAction::Dolly:
            endDolly();
            break;
        case MouseAction::Truck:
            endTruck();
            break;
        case MouseAction::None:
            break;
        }
        mouseAction_ = MouseAction::None;
    }

    // Input layer under the touch and mouse layers. Use these directly for
    // input they do not cover (a trackpad, a custom gesture). Call the end
    // functions when the gesture ends, so that update() goes back from
    // draggingSmoothTime to smoothTime.

    /// One-finger drag. Deltas are (last - current) pixels.
    void rotatePixels(double dxPx, double dyPx, double viewportHeight) {
        rotate(2.0 * kPi * azimuthRotateSpeed * dxPx / viewportHeight,
               2.0 * kPi * polarRotateSpeed * dyPx / viewportHeight, true);
        userRotating_ = true;
    }

    /// Pinch. dollyDeltaPx = previous pinch distance - current, in
    /// density-independent pixels.
    void dollyPinchDelta(double dollyDeltaPx) {
        dollyScaled(dollyDeltaPx * (1.0 / 8.0)); // TOUCH_DOLLY_FACTOR
        userDollying_ = true;
    }

    /// Mouse wheel. deltaY is the wheel delta of a pixel-mode wheel event;
    /// positive scrolls down and dollies out. The web library divides a
    /// macOS wheel delta by 10 and other systems by 30; this function uses
    /// 10, so divide by 3 first to get the other feel.
    void dollyWheelDelta(double deltaY) {
        dollyScaled(deltaY * (1.0 / 10.0));
        userDollying_ = true;
    }

    /// One-finger zoom with an anchor: the world point under the screen
    /// anchor stays in place while the camera dollies.
    void dollyDeltaAnchored(double deltaPx, double anchorXPx, double anchorYPx,
                            double viewportW, double viewportH, double tanHalfFov) {
        dollyAnchored(deltaPx * (1.0 / 8.0), anchorXPx, anchorYPx,
                      viewportW, viewportH, tanHalfFov);
    }

    /// Wheel zoom with an anchor, for zoom-at-cursor on desktop. Same
    /// units as dollyWheelDelta; the anchor is the cursor position.
    void dollyWheelDeltaAnchored(double deltaY, double anchorXPx, double anchorYPx,
                                 double viewportW, double viewportH, double tanHalfFov) {
        dollyAnchored(deltaY * (1.0 / 10.0), anchorXPx, anchorYPx,
                      viewportW, viewportH, tanHalfFov);
    }

    /// Two-finger midpoint drag (the truck half of TOUCH_DOLLY_TRUCK).
    void truckPixels(double dxPx, double dyPx, double viewportHeight,
                     double tanHalfFov) {
        const double targetDistance = radius_ * tanHalfFov;
        const double truckX = truckSpeed * dxPx * targetDistance / viewportHeight;
        const double pedestalY = truckSpeed * dyPx * targetDistance / viewportHeight;
        Vec3 xAxis, yAxis, zAxis;
        basisAxes(xAxis, yAxis, zAxis);
        targetEnd_ += xAxis * truckX + yAxis * (-pedestalY);
        clampTarget();
        userTrucking_ = true;
    }

    void endRotate() { userRotating_ = false; }
    void endDolly() { userDollying_ = false; }
    void endTruck() { userTrucking_ = false; }

    /// Verbatim SmoothDamp from camera-controls (Game Programming Gems 4).
    static double smoothDamp(double current, double targetValue, double &vel,
                             double smoothTime, double maxSpeed, double dt) {
        smoothTime = std::max(0.0001, smoothTime);
        const double omega = 2.0 / smoothTime;
        const double x = omega * dt;
        const double exp = 1.0 / (1.0 + x + 0.48 * x * x + 0.235 * x * x * x);
        double change = current - targetValue;
        const double originalTo = targetValue;
        const double maxChange = maxSpeed * smoothTime;
        change = std::clamp(change, -maxChange, maxChange);
        targetValue = current - change;
        const double temp = (vel + omega * change) * dt;
        vel = (vel - omega * temp) * exp;
        double output = targetValue + (change + temp) * exp;
        if ((originalTo - current > 0.0) == (output > originalTo)) {
            output = originalTo;
            vel = (output - originalTo) / dt;
        }
        return output;
    }

    /// Camera basis for a look-at camera with +Y up. The three vectors are
    /// the columns of the camera model matrix. This uses the three.js
    /// Matrix4.lookAt construction, which is stable at the poles. Some
    /// engine lookAt functions (for example Filament's) change the up
    /// vector near the poles, which rotates the view for one frame.
    struct Basis {
        Vec3 x, y, z;
    };

    static Basis lookAt(const Vec3 &eye, const Vec3 &target) {
        Vec3 zAxis = eye - target;
        if (dot(zAxis, zAxis) == 0.0) zAxis.z = 1.0;
        zAxis = normalize(zAxis);
        Vec3 xAxis = cross(Vec3{0, 1, 0}, zAxis);
        if (dot(xAxis, xAxis) < 1e-24) {
            zAxis.z += 0.0001;
            zAxis = normalize(zAxis);
            xAxis = cross(Vec3{0, 1, 0}, zAxis);
        }
        xAxis = normalize(xAxis);
        return {xAxis, cross(zAxis, xAxis), zAxis};
    }

private:
    static constexpr double kEpsilon = 1e-5;     // camera-controls approxZero
    static constexpr double kMakeSafeEps = 1e-6; // three.js Spherical.makeSafe
    static constexpr double kMaxSpeed = 1e300;

    // Touch layer state. The tuning values are in density-independent
    // pixels and seconds.
    static constexpr int kMaxTouches = 16;
    static constexpr double kTapMaxSeconds = 0.3;
    static constexpr double kTapSlop = 16.0;
    static constexpr double kZoomWindowSeconds = 0.35;
    static constexpr double kZoomNearTap = 60.0;

    struct Touch {
        long long id;
        double x, y, beganX, beganY, beganTime;
    };
    enum class TouchMode { Idle, Rotate, AnchoredZoom, Pinch };

    Touch touches_[kMaxTouches] = {};
    int touchCount_ = 0;
    TouchMode touchMode_ = TouchMode::Idle;
    double viewportW_ = 0, viewportH_ = 0, tanHalfFov_ = 0;
    double grabX_ = 0, grabY_ = 0;
    double zoomY_ = 0;
    double pinchDistance_ = 0, pinchMidX_ = 0, pinchMidY_ = 0;
    double tapTime_ = -1e9, tapX_ = 0, tapY_ = 0;
    MouseAction mouseAction_ = MouseAction::None;
    double mouseX_ = 0, mouseY_ = 0;

    int findTouch(long long touchId) const {
        for (int i = 0; i < touchCount_; i++) {
            if (touches_[i].id == touchId) return i;
        }
        return -1;
    }

    void removeTouch(int index) {
        for (int i = index; i < touchCount_ - 1; i++) touches_[i] = touches_[i + 1];
        touchCount_--;
    }

    void pinchStateNow(double &distance, double &midX, double &midY) const {
        const Touch &a = touches_[0];
        const Touch &b = touches_[1];
        distance = std::hypot(a.x - b.x, a.y - b.y);
        midX = (a.x + b.x) * 0.5;
        midY = (a.y + b.y) * 0.5;
    }

    /// Sets the mode from the touch count and re-baselines the gesture, so
    /// that fingers can join and leave without a jump.
    void syncTouchMode(double timeSeconds) {
        switch (touchCount_) {
        case 0:
            if (touchMode_ == TouchMode::Rotate) {
                endRotate();
            } else if (touchMode_ != TouchMode::Idle) {
                endDolly();
                endTruck();
            }
            touchMode_ = TouchMode::Idle;
            break;
        case 1: {
            if (touchMode_ == TouchMode::Pinch) {
                endDolly();
                endTruck();
            }
            const Touch &t = touches_[0];
            const bool nearTap = std::hypot(t.x - tapX_, t.y - tapY_) < kZoomNearTap;
            if (touchMode_ == TouchMode::Idle && nearTap &&
                timeSeconds - tapTime_ < kZoomWindowSeconds) {
                touchMode_ = TouchMode::AnchoredZoom;
                zoomY_ = t.y;
            } else if (touchMode_ != TouchMode::AnchoredZoom) {
                touchMode_ = TouchMode::Rotate;
                grabX_ = t.x;
                grabY_ = t.y;
            }
            break;
        }
        default:
            if (touchMode_ == TouchMode::Rotate) {
                endRotate();
            } else if (touchMode_ == TouchMode::AnchoredZoom) {
                endDolly();
                endTruck();
            }
            touchMode_ = TouchMode::Pinch;
            pinchStateNow(pinchDistance_, pinchMidX_, pinchMidY_);
            break;
        }
    }

    // Smoothed spherical state (theta about +Y, phi from +Y) and the
    // damping end values, as in the original library's _spherical and
    // _sphericalEnd.
    double theta_ = 0, thetaEnd_ = 0, thetaVel_ = 0;
    double phi_ = kHalfPi, phiEnd_ = kHalfPi, phiVel_ = 0;
    double radius_ = 1, radiusEnd_ = 1, radiusVel_ = 0;
    Vec3 target_, targetEnd_, targetVel_;
    Vec3 boundaryMin_{-kInfinity, -kInfinity, -kInfinity};
    Vec3 boundaryMax_{kInfinity, kInfinity, kInfinity};
    bool userRotating_ = false, userDollying_ = false, userTrucking_ = false;

    static double roundToStep(double value, double step) {
        return std::round(value / step) * step;
    }

    void dollyScaled(double delta) {
        const double scale = std::pow(0.95, -delta * dollySpeed);
        radiusEnd_ = std::clamp(radiusEnd_ * scale, minDistance, maxDistance);
    }

    void dollyAnchored(double delta, double anchorXPx, double anchorYPx,
                       double viewportW, double viewportH, double tanHalfFov) {
        const double oldRadius = radiusEnd_;
        dollyScaled(delta);
        const double applied = oldRadius > 0 ? radiusEnd_ / oldRadius : 1.0;
        userDollying_ = true;

        const double aspect = viewportW / viewportH;
        const double nx = 2.0 * anchorXPx / viewportW - 1.0;
        const double ny = 1.0 - 2.0 * anchorYPx / viewportH;
        const double offsetX = nx * tanHalfFov * aspect * oldRadius;
        const double offsetY = ny * tanHalfFov * oldRadius;

        Vec3 xAxis, yAxis, zAxis;
        basisAxes(xAxis, yAxis, zAxis);
        targetEnd_ += (xAxis * offsetX + yAxis * offsetY) * (1.0 - applied);
        clampTarget();
        userTrucking_ = true;
    }

    void stepAxis(double &value, double end, double &vel, bool dragging, double dt) const {
        if (std::abs(end - value) < kEpsilon) {
            vel = 0;
            value = end;
        } else {
            value = smoothDamp(value, end, vel,
                               dragging ? draggingSmoothTime : smoothTime,
                               kMaxSpeed, dt);
        }
    }

    void clampTarget() {
        targetEnd_ = {std::clamp(targetEnd_.x, boundaryMin_.x, boundaryMax_.x),
                      std::clamp(targetEnd_.y, boundaryMin_.y, boundaryMax_.y),
                      std::clamp(targetEnd_.z, boundaryMin_.z, boundaryMax_.z)};
    }

    void basisAxes(Vec3 &xAxis, Vec3 &yAxis, Vec3 &zAxis) const {
        const double sp = std::sin(phi_), cp = std::cos(phi_);
        const double st = std::sin(theta_), ct = std::cos(theta_);
        zAxis = {sp * st, cp, sp * ct}; // target -> eye
        xAxis = normalize(cross(Vec3{0, 1, 0}, zAxis));
        yAxis = cross(zAxis, xAxis);
    }
};

} // namespace camctl
