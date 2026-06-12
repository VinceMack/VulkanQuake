#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec2 inLightmapUV;
layout(location = 3) in uvec4 inStyles; // <--- The new attribute

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec2 fragLightmapUV;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out flat uvec4 fragStyles; // <--- Passed as flat (no interpolation)

layout(push_constant) uniform PushConstants {
    mat4 renderMatrix;
    vec3 cameraPos;
    float timeOrInterp;
    uint surfaceType;
} pcs;

void main() {
    gl_Position = pcs.renderMatrix * vec4(inPosition, 1.0f);
    fragUV = inUV;
    fragLightmapUV = inLightmapUV;
    fragWorldPos = inPosition;
    fragStyles = inStyles;
}