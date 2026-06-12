#version 450

layout(location = 0) in vec3 inPositionA; // Frame A
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inPositionB; // Frame B (Loc 2!)

layout(location = 0) out vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 renderMatrix;
    vec3 cameraPos;
    float timeOrInterp;
    uint surfaceType;
} pcs;

void main() {
    // Mathematically blend the vertices!
    vec3 blendedPos = mix(inPositionA, inPositionB, pcs.timeOrInterp);
    
    gl_Position = pcs.renderMatrix * vec4(blendedPos, 1.0f);
    fragUV = inUV;
}
