#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec2 inLightmapUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec2 fragLightmapUV;

layout(push_constant) uniform PushConstants {
    mat4 render_matrix;
} pcs;

void main() {
    gl_Position = pcs.render_matrix * vec4(inPosition, 1.0f);
    fragUV = inUV;
    fragLightmapUV = inLightmapUV;
}