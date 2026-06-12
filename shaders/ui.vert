#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 ortho_matrix;
    float dummy; // Padding to match our 3D PushConstants size
} pcs;

void main() {
    // Orthographic projection directly to screen coordinates
    gl_Position = pcs.ortho_matrix * vec4(inPosition, 0.0, 1.0);
    fragUV = inUV;
}
