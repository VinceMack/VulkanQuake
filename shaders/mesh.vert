#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec2 inLightmapUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec2 fragLightmapUV;
layout(location = 2) out vec3 fragWorldPos; // <--- We need the world position for liquid math!

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
}