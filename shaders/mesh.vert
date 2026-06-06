#version 450

// Vertex Shader

// Input from our Vertex Buffer (location 0 matches our RenderVertex struct)
layout(location = 0) in vec3 inPosition;

// We will send the Camera matrix to the GPU every frame using "Push Constants"
layout(push_constant) uniform PushConstants {
    mat4 render_matrix;
} pcs;

void main() {
    // Multiply the 3D position by the camera matrix to get the 2D screen position
    gl_Position = pcs.render_matrix * vec4(inPosition, 1.0f);
}