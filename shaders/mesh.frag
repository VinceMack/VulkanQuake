#version 450

// Fragment Shader

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// Descriptor Set binding
layout(binding = 0) uniform sampler2D texSampler;

void main() {
    // Sample the color from the texture at the given UV coordinate
    outColor = texture(texSampler, fragUV);
}