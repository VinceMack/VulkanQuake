#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec2 fragLightmapUV;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D diffuseSampler;
layout(binding = 1) uniform sampler2D lightmapSampler;

void main() {
    vec4 diffuseColor = texture(diffuseSampler, fragUV);
    vec4 lightmapColor = texture(lightmapSampler, fragLightmapUV);
    
    // Multiply the textures (increase hardcoded '1' value to increase brightness)
    outColor = diffuseColor * lightmapColor * 1;
}