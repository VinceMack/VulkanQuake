#version 450

// Fragment Shader

layout(location = 0) out vec4 outColor;

void main() {
    // Output a solid white/gray color for the walls
    // (RGBA: Red, Green, Blue, Alpha)
    outColor = vec4(0.8, 0.8, 0.8, 1.0);
}