#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    vec4 lightDirection;
} pc;

void main() {
    const vec3 baseColor = vec3(0.25, 0.55, 0.85);
    float diffuse = max(dot(normalize(inNormal), -pc.lightDirection.xyz), 0.0);
    outColor = vec4(baseColor * (0.25 + 0.75 * diffuse), 1.0);
}
