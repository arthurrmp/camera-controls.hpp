// Example for camera-controls.hpp. It does not need a renderer.
//
//   c++ -std=c++17 -I../include minimal.cpp -o minimal && ./minimal
//
// The example simulates a one-finger drag, a release, and a pinch zoom
// through the touch layer. It prints the damped camera state at each step.

#include "camera_controls.hpp"

#include <cstdio>

int main() {
    camctl::CameraControls controls;

    // Frame a subject at (0, 1, 0) from 6 units away, 20 degrees above the
    // horizon.
    controls.minDistance = 0.8;
    controls.maxDistance = 80.0;
    const double polar = camctl::CameraControls::kHalfPi - 0.35;
    controls.setLookAt(/*position*/ 0.0, 1.0 + 6.0 * std::cos(polar), 6.0 * std::sin(polar),
                       /*target*/ 0.0, 1.0, 0.0);

    const double dt = 1.0 / 120.0; // 120Hz tick
    const double fovY = 45.0 * camctl::CameraControls::kPi / 180.0;
    const double aspect = 393.0 / 852.0; // e.g. iPhone points, portrait
    controls.setViewport(393.0, 852.0, std::tan(fovY * 0.5));

    std::printf("-- one finger drags right 20px/frame for 12 frames\n");
    controls.touchBegan(1, 300.0, 400.0, 0.0);
    for (int i = 1; i <= 12; i++) {
        controls.touchMoved(1, 300.0 + 20.0 * i, 400.0);
        controls.update(dt);
    }
    std::printf("   while dragging: azimuthAngle=%.3f rad (end %.3f)\n",
                controls.azimuthAngle(), controls.getSpherical().theta);

    std::printf("-- release; the damped motion continues toward the end value\n");
    controls.touchEnded(1, 1.0);
    for (int i = 1; i <= 30; i++) {
        controls.update(dt);
        if (i % 10 == 0) {
            std::printf("   +%2d frames: azimuthAngle=%.4f rad -> %.4f\n",
                        i, controls.azimuthAngle(), controls.getSpherical().theta);
        }
    }

    std::printf("-- two fingers pinch apart 15px/frame for 10 frames (zoom in)\n");
    controls.touchBegan(2, 150.0, 500.0, 2.0);
    controls.touchBegan(3, 250.0, 500.0, 2.0);
    for (int i = 1; i <= 10; i++) {
        controls.touchMoved(2, 150.0 - 7.5 * i, 500.0);
        controls.touchMoved(3, 250.0 + 7.5 * i, 500.0);
        controls.update(dt);
    }
    controls.touchEnded(2, 3.0);
    controls.touchEnded(3, 3.0);
    for (int i = 0; i < 30; i++) controls.update(dt);
    std::printf("   distance: %.3f (from 6.0)\n", controls.distance());

    std::printf("-- fitToBox: unit cube, portrait viewport\n");
    controls.fitToBox({-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}, false, fovY, aspect);
    std::printf("   distance=%.3f azimuth=%.3f polar=%.3f (nearest 90-degree view)\n",
                controls.distance(), controls.azimuthAngle(), controls.polarAngle());

    std::printf("-- camera basis at the current pose\n");
    const camctl::Vec3 eye = controls.getPosition(false);
    const camctl::Vec3 target = controls.getTarget(false);
    const auto basis = camctl::CameraControls::lookAt(eye, target);
    std::printf("   eye=(%.2f %.2f %.2f) target=(%.2f %.2f %.2f)\n",
                eye.x, eye.y, eye.z, target.x, target.y, target.z);
    std::printf("   X=(%.2f %.2f %.2f) Y=(%.2f %.2f %.2f) Z=(%.2f %.2f %.2f)\n",
                basis.x.x, basis.x.y, basis.x.z,
                basis.y.x, basis.y.y, basis.y.z,
                basis.z.x, basis.z.y, basis.z.z);
    return 0;
}
