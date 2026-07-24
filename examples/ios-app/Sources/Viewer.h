// Objective-C interface over the C++ viewer. Swift talks to this class;
// the C++ (Filament and camera_controls.hpp) stays in Viewer.mm.

#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

NS_ASSUME_NONNULL_BEGIN

@interface Viewer : NSObject

- (instancetype)initWithLayer:(CAMetalLayer *)layer;
- (BOOL)loadModel:(NSData *)glb;
- (BOOL)loadEnvironment:(NSData *)ktx;
- (void)resizeWidth:(uint32_t)width height:(uint32_t)height;
- (void)render:(double)dt;
- (void)shutdown;

// Input. Drag deltas are (last - current). Rotation and truck deltas are in
// physical pixels; dolly deltas are in points. See the library README.
- (void)rotateDx:(double)dx dy:(double)dy;
- (void)pinchDollyDelta:(double)deltaPoints;
- (void)pinchTruckDx:(double)dx dy:(double)dy;
- (void)anchoredDollyDelta:(double)deltaPoints anchorX:(double)x anchorY:(double)y;
- (void)endRotate;
- (void)endPinch;

@end

NS_ASSUME_NONNULL_END
