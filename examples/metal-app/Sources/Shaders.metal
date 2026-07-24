#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 viewProjection;
    float3 lightDirection;
};

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 normal;
};

vertex VertexOut vertexMain(VertexIn in [[stage_in]],
                            constant Uniforms &uniforms [[buffer(1)]]) {
    VertexOut out;
    out.position = uniforms.viewProjection * float4(in.position, 1.0);
    out.normal = in.normal; // the model matrix is the identity
    return out;
}

fragment float4 fragmentMain(VertexOut in [[stage_in]],
                             constant Uniforms &uniforms [[buffer(1)]]) {
    const float3 baseColor = float3(0.25, 0.55, 0.85);
    const float diffuse = max(dot(normalize(in.normal), -uniforms.lightDirection), 0.0);
    return float4(baseColor * (0.25 + 0.75 * diffuse), 1.0);
}
