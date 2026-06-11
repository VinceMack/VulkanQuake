#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    vec4 color = texture(texSampler, fragUV);
    
    // Quake uses palette index 255 for transparency. Our parser set its alpha to 0.0.
    if (color.a < 0.1) {
        discard;
    }
    
    outColor = color;
}
