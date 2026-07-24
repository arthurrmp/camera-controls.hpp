// Objective-C interface over the C++ renderer. The C++ (Metal setup and
// camera_controls.hpp) stays in Renderer.mm.

#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

NS_ASSUME_NONNULL_BEGIN

@interface Renderer : NSObject

- (instancetype)initWithLayer:(CAMetalLayer *)layer;
- (void)resizeWidth:(uint32_t)width height:(uint32_t)height scale:(double)scale;
- (void)render:(double)dt;

// Mouse forwarding, in points. button: 0 left, 1 middle, 2 right.
- (void)mouseDown:(int)button x:(double)x y:(double)y;
- (void)mouseMovedX:(double)x y:(double)y;
- (void)mouseUp;
- (void)mouseWheel:(double)deltaY x:(double)x y:(double)y;

@end

NS_ASSUME_NONNULL_END
