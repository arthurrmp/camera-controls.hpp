// Objective-C interface over the C++ viewer. Swift talks to this class;
// the C++ (Filament and camera_controls.hpp) stays in Viewer.mm.

#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

NS_ASSUME_NONNULL_BEGIN

@interface Viewer : NSObject

- (instancetype)initWithLayer:(CAMetalLayer *)layer;
- (BOOL)loadModel:(NSData *)glb;
- (BOOL)loadEnvironment:(NSData *)ktx;
- (void)resizeWidth:(uint32_t)width height:(uint32_t)height scale:(double)scale;
- (void)render:(double)dt;
- (void)shutdown;

// Touch forwarding. The library decides the gesture (one finger rotates,
// two dolly and truck, double-tap-drag zooms on the tap point).
// Positions are in points; the time is the UITouch timestamp.
- (void)touchBegan:(int64_t)touchId x:(double)x y:(double)y time:(double)time;
- (void)touchMoved:(int64_t)touchId x:(double)x y:(double)y;
- (void)touchEnded:(int64_t)touchId time:(double)time;
- (void)touchCancelled:(int64_t)touchId;

@end

NS_ASSUME_NONNULL_END
