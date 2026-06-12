#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    vec4 color = texture(texSampler, fragUV);
    
    // Quake's font uses palette index 0 (black) for its transparent background!
    if (color.a < 0.1) {
        discard;
    }
    
    outColor = color;
}
