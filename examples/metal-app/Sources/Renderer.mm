#import "Renderer.h"

#include "camera_controls.hpp"

#import <Metal/Metal.h>
#include <simd/simd.h>
#include <cmath>

static constexpr double kFovDegrees = 45.0;

struct Uniforms {
    simd_float4x4 viewProjection;
    simd_float3 lightDirection;
};

struct Vertex {
    simd_float3 position;
    simd_float3 normal;
};

@implementation Renderer {
    CAMetalLayer *_layer;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _queue;
    id<MTLRenderPipelineState> _pipeline;
    id<MTLDepthStencilState> _depthState;
    id<MTLTexture> _depthTexture;
    id<MTLBuffer> _vertexBuffer;
    id<MTLBuffer> _indexBuffer;
    NSUInteger _indexCount;
    camctl::CameraControls _controls;
    double _tanHalfFov;
    uint32_t _width;
    uint32_t _height;
    BOOL _framed;
}

- (instancetype)initWithLayer:(CAMetalLayer *)layer {
    self = [super init];
    if (!self) return nil;

    _layer = layer;
    _device = MTLCreateSystemDefaultDevice();
    _layer.device = _device;
    _layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    _queue = [_device newCommandQueue];
    _tanHalfFov = std::tan(kFovDegrees * 0.5 * M_PI / 180.0);

    [self buildPipeline];
    [self buildCube];

    _controls.minDistance = 1.0;
    _controls.maxDistance = 60.0;
    return self;
}

- (void)buildPipeline {
    id<MTLLibrary> library = [_device newDefaultLibrary];
    MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = [library newFunctionWithName:@"vertexMain"];
    desc.fragmentFunction = [library newFunctionWithName:@"fragmentMain"];
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    MTLVertexDescriptor *vertexDesc = [MTLVertexDescriptor new];
    vertexDesc.attributes[0].format = MTLVertexFormatFloat3;
    vertexDesc.attributes[0].offset = offsetof(Vertex, position);
    vertexDesc.attributes[0].bufferIndex = 0;
    vertexDesc.attributes[1].format = MTLVertexFormatFloat3;
    vertexDesc.attributes[1].offset = offsetof(Vertex, normal);
    vertexDesc.attributes[1].bufferIndex = 0;
    vertexDesc.layouts[0].stride = sizeof(Vertex);
    desc.vertexDescriptor = vertexDesc;

    NSError *error = nil;
    _pipeline = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!_pipeline) NSLog(@"pipeline error: %@", error);

    MTLDepthStencilDescriptor *depthDesc = [MTLDepthStencilDescriptor new];
    depthDesc.depthCompareFunction = MTLCompareFunctionLess;
    depthDesc.depthWriteEnabled = YES;
    _depthState = [_device newDepthStencilStateWithDescriptor:depthDesc];
}

- (void)buildCube {
    // 24 vertices (one normal per face) and 36 indices.
    static const simd_float3 normals[6] = {
        {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
    };
    Vertex vertices[24];
    uint16_t indices[36];
    int v = 0, i = 0;
    for (int face = 0; face < 6; face++) {
        const simd_float3 n = normals[face];
        // Two axes orthogonal to the face normal.
        const simd_float3 up = std::abs(n.y) > 0.5f
            ? simd_make_float3(0, 0, 1) : simd_make_float3(0, 1, 0);
        const simd_float3 right = simd_cross(up, n);
        const simd_float3 realUp = simd_cross(n, right);
        const int base = v;
        vertices[v++] = {n - right - realUp, n};
        vertices[v++] = {n + right - realUp, n};
        vertices[v++] = {n + right + realUp, n};
        vertices[v++] = {n - right + realUp, n};
        indices[i++] = base; indices[i++] = base + 1; indices[i++] = base + 2;
        indices[i++] = base; indices[i++] = base + 2; indices[i++] = base + 3;
    }
    _vertexBuffer = [_device newBufferWithBytes:vertices
                                         length:sizeof(vertices)
                                        options:MTLResourceStorageModeShared];
    _indexBuffer = [_device newBufferWithBytes:indices
                                        length:sizeof(indices)
                                       options:MTLResourceStorageModeShared];
    _indexCount = 36;
}

- (void)resizeWidth:(uint32_t)width height:(uint32_t)height scale:(double)scale {
    if (width == 0 || height == 0 || scale <= 0) return;
    _width = width;
    _height = height;
    _controls.setViewport(width / scale, height / scale, _tanHalfFov);
    if (!_framed) {
        // Frame the cube: fit its bounding sphere, then 20 degrees above
        // the horizon.
        _framed = YES;
        _controls.fitToSphere({0, 0, 0}, std::sqrt(3.0), false,
                              kFovDegrees * M_PI / 180.0,
                              (double)width / (double)height);
        _controls.rotateTo(0.0, 70.0 * M_PI / 180.0, false);
    }

    MTLTextureDescriptor *desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:width
                                    height:height
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;
    _depthTexture = [_device newTextureWithDescriptor:desc];
}

/// The view matrix comes from the lookAt basis: the basis vectors are the
/// columns of the camera model matrix, so the view matrix (its inverse)
/// has them as rows, with -dot(axis, eye) as the translation.
static simd_float4x4 viewMatrix(const camctl::CameraControls::Basis &b,
                                const camctl::Vec3 &eye) {
    const simd_double3 x = {b.x.x, b.x.y, b.x.z};
    const simd_double3 y = {b.y.x, b.y.y, b.y.z};
    const simd_double3 z = {b.z.x, b.z.y, b.z.z};
    const simd_double3 e = {eye.x, eye.y, eye.z};
    return simd_matrix(
        simd_make_float4(x.x, y.x, z.x, 0),
        simd_make_float4(x.y, y.y, z.y, 0),
        simd_make_float4(x.z, y.z, z.z, 0),
        simd_make_float4(-simd_dot(x, e), -simd_dot(y, e), -simd_dot(z, e), 1));
}

static simd_float4x4 projectionMatrix(double fovYRadians, double aspect,
                                      double nearPlane, double farPlane) {
    const float ys = 1.0 / std::tan(fovYRadians * 0.5);
    const float xs = ys / aspect;
    const float zs = farPlane / (nearPlane - farPlane);
    return simd_matrix(
        simd_make_float4(xs, 0, 0, 0),
        simd_make_float4(0, ys, 0, 0),
        simd_make_float4(0, 0, zs, -1),
        simd_make_float4(0, 0, nearPlane * zs, 0));
}

- (void)render:(double)dt {
    if (_width == 0 || _height == 0) return;
    _controls.update(dt);
    const camctl::Vec3 eye = _controls.getPosition(false);
    const camctl::Vec3 target = _controls.getTarget(false);
    const auto basis = camctl::CameraControls::lookAt(eye, target);

    Uniforms uniforms;
    const simd_float4x4 view = viewMatrix(basis, eye);
    const simd_float4x4 projection = projectionMatrix(
        kFovDegrees * M_PI / 180.0, (double)_width / (double)_height, 0.1, 200.0);
    uniforms.viewProjection = simd_mul(projection, view);
    uniforms.lightDirection = simd_normalize(simd_make_float3(-0.5, -1.0, -0.6));

    id<CAMetalDrawable> drawable = [_layer nextDrawable];
    if (!drawable) return;

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.05, 0.05, 0.07, 1.0);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.depthAttachment.texture = _depthTexture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.clearDepth = 1.0;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;

    id<MTLCommandBuffer> commands = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:_pipeline];
    [encoder setDepthStencilState:_depthState];
    [encoder setVertexBuffer:_vertexBuffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:_indexCount
                         indexType:MTLIndexTypeUInt16
                       indexBuffer:_indexBuffer
                 indexBufferOffset:0];
    [encoder endEncoding];
    [commands presentDrawable:drawable];
    [commands commit];
}

- (void)mouseDown:(int)button x:(double)x y:(double)y {
    _controls.mouseDown(button, x, y);
}

- (void)mouseMovedX:(double)x y:(double)y {
    _controls.mouseMoved(x, y);
}

- (void)mouseUp {
    _controls.mouseUp();
}

- (void)mouseWheel:(double)deltaY x:(double)x y:(double)y {
    _controls.mouseWheel(deltaY, x, y);
}

@end
