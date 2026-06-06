#version 450

// Vertex Shader

// Input from our RenderVertex struct
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
// (location 2 is textureId, but since we are batching on the CPU, the vertex shader doesn't actually need it!)

// We will send the Camera matrix to the GPU every frame using "Push Constants"
// Output to the fragment shader
layout(location = 0) out vec2 fragUV;
layout(push_constant) uniform PushConstants {
    mat4 render_matrix;
} pcs;

void main() {
    // Multiply the 3D position by the camera matrix to get the 2D screen position
    gl_Position = pcs.render_matrix * vec4(inPosition, 1.0f);
    fragUV = inUV; // Pass the UVs through
}